//! Native GraphVis renderer.
//!
//! WGPU presents directly into a native child window owned by the Qt/QML shell.
//! No rendered RGBA frame is read back through Python or Qt.  Native scientific
//! geometry kernels live here as well so interactive contours/voxels/meshes do
//! not require a Python round trip.

use anyhow::{Result, anyhow};
// Context and DeviceExt are used only inside from_win32 and from_surface, both
// of which are Windows-only. Gated rather than removed: on Windows they are
// load-bearing, and this is the platform that ships.
#[cfg(target_os = "windows")]
use anyhow::Context;
use bytemuck::{Pod, Zeroable};
use glam::{Mat4, Vec3};
use rayon::prelude::*;
// Windows-only: the sole use is from_win32, which is itself behind this cfg.
// Deleting the import to silence the Linux warning would break the Windows
// build, which is the one that ships.
#[cfg(target_os = "windows")]
use std::num::NonZeroIsize;
#[cfg(target_os = "windows")]
use wgpu::util::DeviceExt;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
pub struct PointVertex {
    pub position: [f32; 3],
    pub response: f32,
    pub size: f32,
    pub alpha: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct CameraUniform {
    view_proj: [[f32; 4]; 4],
    view: [f32; 4],
    style: [f32; 4],
}

#[derive(Debug, Clone, Copy)]
pub struct CameraState {
    pub azimuth_deg: f32,
    pub elevation_deg: f32,
    pub pan_x: f32,
    pub pan_y: f32,
    pub zoom: f32,
}
impl Default for CameraState {
    fn default() -> Self {
        Self {
            azimuth_deg: -40.0,
            elevation_deg: 25.0,
            pan_x: 0.0,
            pan_y: 0.0,
            zoom: 1.0,
        }
    }
}

pub struct NativeSurfaceRenderer {
    _instance: wgpu::Instance,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pipeline: wgpu::RenderPipeline,
    camera_buffer: wgpu::Buffer,
    camera_bind_group: wgpu::BindGroup,
    depth_texture: wgpu::Texture,
    depth_view: wgpu::TextureView,
    point_buffer: Option<wgpu::Buffer>,
    point_capacity_bytes: u64,
    point_count: u32,
    camera: CameraState,
    point_scale: f32,
    alpha_scale: f32,
    invert_opacity: bool,
    min_alpha: f32,
    colormap_id: f32,
    clear: wgpu::Color,
}

impl NativeSurfaceRenderer {
    /// Attach WGPU directly to the HWND supplied by a Qt child QWindow.
    /// The surface is presented by WGPU; Qt never receives an RGBA frame.
    /// Build a renderer drawing directly into an existing native window.
    ///
    /// # Safety
    ///
    /// `hwnd` must be a valid HWND and `hinstance` its module handle, and the
    /// caller must keep that window alive for the whole life of the returned
    /// renderer. The surface borrows the window without owning it, so a window
    /// destroyed first leaves the GPU surface pointing at freed memory.
    #[cfg(target_os = "windows")]
    pub unsafe fn from_win32(
        hwnd: isize,
        hinstance: isize,
        width: u32,
        height: u32,
    ) -> Result<Self> {
        use raw_window_handle::{
            RawDisplayHandle, RawWindowHandle, Win32WindowHandle, WindowsDisplayHandle,
        };
        let hwnd = NonZeroIsize::new(hwnd).context("null HWND")?;
        let mut wh = Win32WindowHandle::new(hwnd);
        wh.hinstance = NonZeroIsize::new(hinstance);
        let target = wgpu::SurfaceTargetUnsafe::RawHandle {
            raw_display_handle: Some(RawDisplayHandle::Windows(WindowsDisplayHandle::new())),
            raw_window_handle: RawWindowHandle::Win32(wh),
        };
        let instance = wgpu::Instance::default();
        // SAFETY: the caller keeps the native Qt child window alive for the renderer's lifetime.
        let surface = unsafe { instance.create_surface_unsafe(target)? };
        Self::from_surface(instance, surface, width, height)
    }

    /// Build a renderer drawing directly into an existing native window.
    ///
    /// # Safety
    ///
    /// Nothing is dereferenced on this platform - the call fails immediately -
    /// but the signature must match the Windows one, so it keeps the same
    /// contract: the handles must be valid and the window must outlive the
    /// renderer.
    #[cfg(not(target_os = "windows"))]
    pub unsafe fn from_win32(_: isize, _: isize, _: u32, _: u32) -> Result<Self> {
        Err(anyhow!(
            "Win32 direct-surface constructor is only available on Windows"
        ))
    }

    // Reached only from from_win32, so on any other platform it is genuinely
    // dead rather than merely unreferenced.
    #[cfg(target_os = "windows")]
    fn from_surface(
        instance: wgpu::Instance,
        surface: wgpu::Surface<'static>,
        width: u32,
        height: u32,
    ) -> Result<Self> {
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
            apply_limit_buckets: false,
        }))?;
        let (device, queue) =
            pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor::default()))?;
        let caps = surface.get_capabilities(&adapter);
        let mut config = surface
            .get_default_config(&adapter, width.max(2), height.max(2))
            .context("surface has no default configuration")?;
        config.present_mode = if caps.present_modes.contains(&wgpu::PresentMode::Mailbox) {
            wgpu::PresentMode::Mailbox
        } else {
            wgpu::PresentMode::Fifo
        };
        config.desired_maximum_frame_latency = 2;
        let format = config.format;
        surface.configure(&device, &config);
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("GraphVis native point cloud"),
            source: wgpu::ShaderSource::Wgsl(include_str!("../shaders/point_cloud.wgsl").into()),
        });
        let bind_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("GraphVis camera layout"),
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
        });
        let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("GraphVis native pipeline layout"),
            bind_group_layouts: &[Some(&bind_layout)],
            immediate_size: 0,
        });
        const ATTRS: [wgpu::VertexAttribute; 4] =
            wgpu::vertex_attr_array![0=>Float32x3, 1=>Float32, 2=>Float32, 3=>Float32];
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("GraphVis native point pipeline"),
            layout: Some(&layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[Some(wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<PointVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Instance,
                    attributes: &ATTRS,
                })],
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: wgpu::TextureFormat::Depth32Float,
                depth_write_enabled: Some(true),
                depth_compare: Some(wgpu::CompareFunction::LessEqual),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: Default::default(),
            multiview_mask: None,
            cache: None,
        });
        let camera_uniform = CameraUniform {
            view_proj: Mat4::IDENTITY.to_cols_array_2d(),
            view: [width as f32, height as f32, 1.0, 0.85],
            style: [0.0, 0.05, 0.0, 0.0],
        };
        let camera_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("GraphVis persistent camera uniform"),
            contents: bytemuck::bytes_of(&camera_uniform),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });
        let camera_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("GraphVis persistent camera bind"),
            layout: &bind_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: camera_buffer.as_entire_binding(),
            }],
        });
        let (depth_texture, depth_view) = Self::create_depth(&device, width.max(2), height.max(2));
        Ok(Self {
            _instance: instance,
            surface,
            device,
            queue,
            config,
            pipeline,
            camera_buffer,
            camera_bind_group,
            depth_texture,
            depth_view,
            point_buffer: None,
            point_capacity_bytes: 0,
            point_count: 0,
            camera: CameraState::default(),
            point_scale: 5.0,
            alpha_scale: 0.85,
            invert_opacity: false,
            min_alpha: 0.05,
            colormap_id: 0.0,
            clear: wgpu::Color {
                r: 0.027,
                g: 0.035,
                b: 0.047,
                a: 1.0,
            },
        })
    }

    fn create_depth(
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> (wgpu::Texture, wgpu::TextureView) {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("GraphVis depth buffer"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let view = texture.create_view(&Default::default());
        (texture, view)
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if width < 2 || height < 2 {
            return;
        }
        self.config.width = width;
        self.config.height = height;
        self.surface.configure(&self.device, &self.config);
        let (texture, view) = Self::create_depth(&self.device, width, height);
        self.depth_texture = texture;
        self.depth_view = view;
    }

    pub fn set_camera(&mut self, camera: CameraState) {
        self.camera = camera;
    }
    pub fn set_style(&mut self, point_scale: f32, alpha_scale: f32, invert_opacity: bool) {
        self.point_scale = point_scale.max(1.0);
        self.alpha_scale = alpha_scale.clamp(0.0, 1.0);
        self.invert_opacity = invert_opacity;
    }
    /// Apply a Smart Render style without rebuilding geometry or GPU pipeline objects.
    pub fn set_smart_style(
        &mut self,
        point_scale: f32,
        alpha_scale: f32,
        invert_opacity: bool,
        min_alpha: f32,
        colormap_id: u32,
    ) {
        self.set_style(point_scale, alpha_scale, invert_opacity);
        self.min_alpha = min_alpha.clamp(0.0, 0.5);
        self.colormap_id = (colormap_id.min(3)) as f32;
    }

    pub fn set_points(&mut self, points: &[PointVertex]) {
        self.point_count = points.len().min(u32::MAX as usize) as u32;
        if points.is_empty() {
            return;
        }
        let bytes = bytemuck::cast_slice(points);
        let required = bytes.len() as u64;
        if self.point_buffer.is_none() || required > self.point_capacity_bytes {
            let capacity = required.next_power_of_two().max(4096);
            self.point_buffer = Some(self.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("GraphVis persistent point buffer"),
                size: capacity,
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            }));
            self.point_capacity_bytes = capacity;
        }
        if let Some(buffer) = &self.point_buffer {
            self.queue.write_buffer(buffer, 0, bytes);
        }
    }

    pub fn render(&mut self) -> Result<()> {
        let (frame, reconfigure_after_present) = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame) => (frame, false),
            wgpu::CurrentSurfaceTexture::Suboptimal(frame) => (frame, true),
            wgpu::CurrentSurfaceTexture::Timeout | wgpu::CurrentSurfaceTexture::Occluded => {
                return Ok(());
            }
            wgpu::CurrentSurfaceTexture::Outdated | wgpu::CurrentSurfaceTexture::Lost => {
                self.surface.configure(&self.device, &self.config);
                match self.surface.get_current_texture() {
                    wgpu::CurrentSurfaceTexture::Success(frame) => (frame, false),
                    wgpu::CurrentSurfaceTexture::Suboptimal(frame) => (frame, true),
                    wgpu::CurrentSurfaceTexture::Timeout
                    | wgpu::CurrentSurfaceTexture::Occluded => return Ok(()),
                    wgpu::CurrentSurfaceTexture::Outdated => {
                        return Err(anyhow!("surface remained outdated after reconfiguration"));
                    }
                    wgpu::CurrentSurfaceTexture::Lost => {
                        return Err(anyhow!("surface was lost after reconfiguration"));
                    }
                    wgpu::CurrentSurfaceTexture::Validation => {
                        return Err(anyhow!("surface acquisition validation error"));
                    }
                }
            }
            wgpu::CurrentSurfaceTexture::Validation => {
                return Err(anyhow!("surface acquisition validation error"));
            }
        };
        let view = frame.texture.create_view(&Default::default());
        let rotation = Mat4::from_rotation_x(self.camera.elevation_deg.to_radians())
            * Mat4::from_rotation_z(self.camera.azimuth_deg.to_radians());
        let aspect = self.config.width as f32 / self.config.height.max(1) as f32;
        let proj = Mat4::perspective_rh_gl(45f32.to_radians(), aspect, 0.01, 100.0);
        let eye = Vec3::new(
            0.0,
            -3.2 / self.camera.zoom.clamp(0.1, 12.0),
            1.8 / self.camera.zoom.clamp(0.1, 12.0),
        );
        let view_mat = Mat4::look_at_rh(eye, Vec3::ZERO, Vec3::Z);
        let pan = Mat4::from_translation(Vec3::new(self.camera.pan_x, self.camera.pan_y, 0.0));
        let uniform = CameraUniform {
            view_proj: (proj * view_mat * pan * rotation).to_cols_array_2d(),
            view: [
                self.config.width as f32,
                self.config.height as f32,
                self.point_scale,
                self.alpha_scale,
            ],
            style: [
                if self.invert_opacity { 1.0 } else { 0.0 },
                self.min_alpha,
                self.colormap_id,
                0.0,
            ],
        };
        self.queue
            .write_buffer(&self.camera_buffer, 0, bytemuck::bytes_of(&uniform));
        let mut encoder = self.device.create_command_encoder(&Default::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("GraphVis direct present"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(self.clear),
                        store: wgpu::StoreOp::Store,
                    },
                    depth_slice: None,
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &self.depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(1.0),
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            if let Some(points) = &self.point_buffer {
                pass.set_pipeline(&self.pipeline);
                pass.set_bind_group(0, &self.camera_bind_group, &[]);
                pass.set_vertex_buffer(0, points.slice(..));
                pass.draw(0..6, 0..self.point_count);
            }
        }
        self.queue.submit(Some(encoder.finish()));
        drop(frame);
        if reconfigure_after_present {
            self.surface.configure(&self.device, &self.config);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Copy)]
pub struct VoxelCell {
    pub center: [f64; 3],
    pub mean: f64,
    pub count: u32,
}

/// Native voxel aggregation preserving density and mean response.
pub fn voxel_aggregate(
    x: &[f64],
    y: &[f64],
    z: &[f64],
    response: &[f64],
    bins: [usize; 3],
) -> Vec<VoxelCell> {
    let n = x.len().min(y.len()).min(z.len()).min(response.len());
    if n == 0 || bins.contains(&0) {
        return vec![];
    }
    fn extent(v: &[f64]) -> Option<(f64, f64)> {
        let mut lo = f64::INFINITY;
        let mut hi = f64::NEG_INFINITY;
        for &q in v {
            if q.is_finite() {
                lo = lo.min(q);
                hi = hi.max(q);
            }
        }
        lo.is_finite().then_some((lo, hi))
    }
    let Some((xl, xh)) = extent(&x[..n]) else {
        return vec![];
    };
    let Some((yl, yh)) = extent(&y[..n]) else {
        return vec![];
    };
    let Some((zl, zh)) = extent(&z[..n]) else {
        return vec![];
    };
    let total = bins[0] * bins[1] * bins[2];
    let accum = (0..n)
        .into_par_iter()
        .fold(
            || vec![(0.0f64, 0u32); total],
            |mut a, i| {
                let vals = [x[i], y[i], z[i], response[i]];
                if vals.iter().all(|v| v.is_finite()) {
                    let ix = (((x[i] - xl) / (xh - xl).max(f64::EPSILON)) * bins[0] as f64)
                        .floor()
                        .clamp(0.0, (bins[0] - 1) as f64) as usize;
                    let iy = (((y[i] - yl) / (yh - yl).max(f64::EPSILON)) * bins[1] as f64)
                        .floor()
                        .clamp(0.0, (bins[1] - 1) as f64) as usize;
                    let iz = (((z[i] - zl) / (zh - zl).max(f64::EPSILON)) * bins[2] as f64)
                        .floor()
                        .clamp(0.0, (bins[2] - 1) as f64) as usize;
                    let k = ix + bins[0] * (iy + bins[1] * iz);
                    a[k].0 += response[i];
                    a[k].1 += 1;
                }
                a
            },
        )
        .reduce(
            || vec![(0.0, 0); total],
            |mut a, b| {
                for (i, (s, c)) in b.into_iter().enumerate() {
                    a[i].0 += s;
                    a[i].1 += c;
                }
                a
            },
        );
    accum
        .into_iter()
        .enumerate()
        .filter_map(|(k, (sum, count))| {
            if count == 0 {
                return None;
            };
            let ix = k % bins[0];
            let iy = (k / bins[0]) % bins[1];
            let iz = k / (bins[0] * bins[1]);
            Some(VoxelCell {
                center: [
                    xl + (ix as f64 + 0.5) * (xh - xl) / bins[0] as f64,
                    yl + (iy as f64 + 0.5) * (yh - yl) / bins[1] as f64,
                    zl + (iz as f64 + 0.5) * (zh - zl) / bins[2] as f64,
                ],
                mean: sum / count as f64,
                count,
            })
        })
        .collect()
}

// triangulate_grid() and marching_squares() used to be here, and nothing called
// either of them from Rust, from C++ or from the app. Both algorithms are
// implemented again in native/plot2d - the surface engines triangulate a grid
// in QtPlotBackend, and drawContour() is a marching-squares sweep over the
// cached value grid - and those are the copies that draw what the user sees.
// One algorithm in two languages, with only one of them reachable, is a bug
// waiting for the day someone fixes the copy that is not running.
