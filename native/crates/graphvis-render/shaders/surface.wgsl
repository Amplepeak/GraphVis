struct Camera { view_proj: mat4x4<f32>, light: vec4<f32> };
@group(0) @binding(0) var<uniform> camera: Camera;
struct In { @location(0) position: vec3<f32>, @location(1) normal: vec3<f32>, @location(2) value: f32 };
struct Out { @builtin(position) pos: vec4<f32>, @location(0) normal: vec3<f32>, @location(1) value: f32 };
@vertex fn vs_main(v: In) -> Out { var o:Out; o.pos=camera.view_proj*vec4<f32>(v.position,1.0); o.normal=v.normal; o.value=v.value; return o; }
@fragment fn fs_main(i:Out)->@location(0) vec4<f32>{
 let n=normalize(i.normal); let l=normalize(camera.light.xyz); let diffuse=max(dot(n,l),0.1);
 let base=vec3<f32>(0.12+0.75*i.value,0.25+0.55*(1.0-i.value),0.75-0.50*i.value);
 return vec4<f32>(base*diffuse,1.0);
}
