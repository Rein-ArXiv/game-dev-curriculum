// graphics_dx_lab cube.hlsl — Per-Draw + Instanced 둘 다 처리.
//
// Root signature 와 동기:
//   b0 (24 DWORD)  : [useInstancing(1), pad(3), mat4 model(16), vec4 color(4)]
//                    — cbuffer 패킹 규칙상 float4x4 는 16바이트 경계에서 시작하므로
//                      uint 뒤에 3 DWORD 패딩이 생긴다. C++ 측 오프셋(4, 20)과 동기.
//   b1 (CBV)       : FrameCB.
//   t0 (Root SRV)  : StructuredBuffer<Instance> for Instanced 모드.
//
// 행렬 규약: 기본(column_major) 선언 + C++ 에서 XMMatrixTranspose 후 업로드.
//   DXMath 행벡터 행렬 M 의 전치를 row-major 메모리에 쓰면 column_major 해석으로
//   M 그대로 읽혀 mul(v, M) = v·M 이 성립하는 표준 D3D 관용구다.
//   (row_major 선언 + Transpose 를 동시에 쓰면 이중 변환으로 깨진다.)

cbuffer FrameCB : register(b1) {
    float4x4 viewProj;
    float3   cameraPos;
    float    time;
    float3   lightDir;
    float    _pad;
};

cbuffer DrawCB : register(b0) {
    uint     useInstancing;
    float4x4 model;
    float4   color;
};

struct Instance {
    float4x4 model;
    float4   color;
};
StructuredBuffer<Instance> instances : register(t0);

struct VSInput {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput {
    float4 pos      : SV_Position;
    float3 normalWS : NORMAL;
    float3 worldPos : POSITION;
    float3 color    : COLOR;
};

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID) {
    VSOutput o;
    float4x4 m;
    float4   c;
    if (useInstancing == 1) {
        m = instances[iid].model;
        c = instances[iid].color;
    } else {
        m = model;
        c = color;
    }
    float4 worldPos = mul(float4(input.pos, 1.0), m);
    o.pos      = mul(worldPos, viewProj);
    o.worldPos = worldPos.xyz;
    o.normalWS = mul(float4(input.normal, 0.0), m).xyz;
    o.color    = c.rgb;
    return o;
}

float4 PSLambert(VSOutput input) : SV_Target {
    float3 N = normalize(input.normalWS);
    float3 L = normalize(lightDir);
    float diff = max(dot(N, L), 0.0);
    return float4(input.color * (0.15 + diff), 1.0);
}

float4 PSBlinnPhong(VSOutput input) : SV_Target {
    float3 N = normalize(input.normalWS);
    float3 L = normalize(lightDir);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    return float4(input.color * (0.15 + diff) + 0.5 * spec, 1.0);
}
