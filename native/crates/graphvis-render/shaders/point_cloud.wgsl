struct Camera {
    view_proj: mat4x4<f32>,
    view: vec4<f32>,        // viewport x/y, point scale, alpha scale
    style: vec4<f32>,       // invert opacity, minimum alpha, colormap id, reserved
};
@group(0) @binding(0) var<uniform> camera: Camera;

struct InstanceIn {
    @location(0) position: vec3<f32>,
    @location(1) response: f32,
    @location(2) size: f32,
    @location(3) alpha: f32,
};
struct Out {
    @builtin(position) pos: vec4<f32>,
    @location(0) response: f32,
    @location(1) alpha: f32,
    @location(2) local: vec2<f32>,
};

fn turbo(t0: f32) -> vec3<f32> {
    let t = clamp(t0, 0.0, 1.0);
    let r = 0.13572138 + t*(4.61539260 + t*(-42.66032258 + t*(132.13108234 + t*(-152.94239396 + t*59.28637943))));
    let g = 0.09140261 + t*(2.19418839 + t*(4.84296658 + t*(-14.18503333 + t*(4.27729857 + t*2.82956604))));
    let b = 0.10667330 + t*(12.64194608 + t*(-60.58204836 + t*(110.36276771 + t*(-89.90310912 + t*27.34824973))));
    return clamp(vec3<f32>(r,g,b), vec3<f32>(0.0), vec3<f32>(1.0));
}
fn viridis(t0:f32)->vec3<f32>{
    let t=clamp(t0,0.0,1.0);
    let c0=vec3<f32>(0.267004,0.004874,0.329415);let c1=vec3<f32>(0.229739,0.322361,0.545706);
    let c2=vec3<f32>(0.127568,0.566949,0.550556);let c3=vec3<f32>(0.369214,0.788888,0.382914);let c4=vec3<f32>(0.993248,0.906157,0.143936);
    if(t<0.25){return mix(c0,c1,t*4.0);} if(t<0.5){return mix(c1,c2,(t-.25)*4.0);} if(t<0.75){return mix(c2,c3,(t-.5)*4.0);} return mix(c3,c4,(t-.75)*4.0);
}
fn plasma(t0:f32)->vec3<f32>{
    let t=clamp(t0,0.0,1.0);let c0=vec3<f32>(0.050383,0.029803,0.527975);let c1=vec3<f32>(0.494877,0.011990,0.657865);let c2=vec3<f32>(0.798216,0.280197,0.469538);let c3=vec3<f32>(0.973416,0.585761,0.251540);let c4=vec3<f32>(0.940015,0.975158,0.131326);
    if(t<0.25){return mix(c0,c1,t*4.0);} if(t<0.5){return mix(c1,c2,(t-.25)*4.0);} if(t<0.75){return mix(c2,c3,(t-.5)*4.0);} return mix(c3,c4,(t-.75)*4.0);
}
fn coolwarm(t0:f32)->vec3<f32>{
    let t=clamp(t0,0.0,1.0);let cool=vec3<f32>(0.2298,0.2987,0.7537);let mid=vec3<f32>(0.865,0.865,0.865);let warm=vec3<f32>(0.7057,0.0156,0.1502);
    return select(mix(mid,warm,(t-.5)*2.0),mix(cool,mid,t*2.0),t<.5);
}
fn palette(t:f32)->vec3<f32>{let id=u32(camera.style.z+0.5);if(id==1u){return viridis(t);}if(id==2u){return plasma(t);}if(id==3u){return coolwarm(t);}return turbo(t);}

@vertex fn vs_main(v: InstanceIn, @builtin(vertex_index) vid: u32) -> Out {
    let corners = array<vec2<f32>, 6>(
        vec2<f32>(-1.0,-1.0), vec2<f32>( 1.0,-1.0), vec2<f32>( 1.0, 1.0),
        vec2<f32>(-1.0,-1.0), vec2<f32>( 1.0, 1.0), vec2<f32>(-1.0, 1.0)
    );
    let corner = corners[vid];
    let base = camera.view_proj * vec4<f32>(v.position, 1.0);
    let px = max(1.0, v.size * camera.view.z);
    let ndc = vec2<f32>(2.0 * px / max(camera.view.x, 1.0), 2.0 * px / max(camera.view.y, 1.0));
    let mapped_alpha = select(v.response, 1.0 - v.response, camera.style.x > 0.5);
    var o: Out;
    o.pos = base + vec4<f32>(corner * ndc * base.w, 0.0, 0.0);
    o.response = v.response;
    o.alpha = clamp(v.alpha * camera.view.w * max(camera.style.y, mapped_alpha), 0.0, 1.0);
    o.local = corner;
    return o;
}

@fragment fn fs_main(i: Out) -> @location(0) vec4<f32> {
    if (dot(i.local, i.local) > 1.0) { discard; }
    return vec4<f32>(palette(i.response), i.alpha);
}
