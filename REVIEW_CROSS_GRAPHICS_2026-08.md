# 교차 대조 검수 보고서 — graphics 내부 (2026-08)

- 대상: `graphics/` 41편. **편별 검수를 마친 뒤**, 여러 문서가 같은 주제를 다룰 때
  서로 모순되는 서술을 하는 곳을 찾는 별도 패스다.
- 주제 5개: NDC·좌표계 규약 / PBR 수식 / 감마·색공간 / 동기화·배리어 / GPU 아키텍처·래스터화·깊이
- **API마다 규약이 다른 것은 모순이 아니다.** 모순의 기준은 (a) 같은 API 안에서 두 편이 다르거나,
  (b) 자기 API가 아닌 규약을 적용했거나, (c) stage0 기초편과 구체 API편이 양립 불가할 때다.
- **검수(2026-08) 시점에는 지적만 하고 본문을 수정하지 않았다. 그러나 2026-08-13 반영 작업에서 아래 19건이 전부 본문에 반영됐다.** 상태는 각 지적 제목 옆의 표시와 맨 끝 「반영 상태 (2026-08-13)」 절을 보라.

> 총 **19건**. NDC 6 · PBR 4 · 감마 3 · 배리어 2 · GPU 4.
> math↔physics 대조와 마찬가지로 **한 문서 안에서 두 절이 서로 다른 규약·값을 쓰는** 형태가 다수다.

---

## NDC · 좌표계 규약 — 6건

### 1. 문서간모순 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_01_cube.md, graphics/stage1_gl_fund_02_transform.md

**위치**: stage1_gl_proj_01_cube.md:359 / stage1_gl_fund_02_transform.md:49, 128

```text
gl_proj_01_cube:359 — "`glUniformMatrix4fv(loc, count, transpose, ptr)`의 `transpose = GL_FALSE`가 *column-major* 입력을 뜻한다. GLM이 column-major라 일치한다. *DirectX 측에서 가져온 row-major 행렬* 을 그대로 넣으려면 `GL_TRUE`로 전치를 요청한다."  ///  gl_fund_02:49 — "DirectXMath의 전형적인 행벡터 행렬을 GLSL의 열벡터 식에 재사용하면 필요한 수학적 행렬은 원본의 전치이고, 원시 바이트를 column-major로 해석하는 과정이 이미 그 전치 효과를 낼 수도 있다. 투영 깊이·손잡이까지 다를 수 있으므로 라이브러리 이름만 보고 `GL_TRUE`를 선택하면 안 된다."
```

gl_fund_02가 "라이브러리 이름만 보고 GL_TRUE를 선택하면 안 된다"고 명시적으로 함정으로 지목한 바로 그 조언을 gl_proj_01_cube가 그대로 처방한다. 같은 OpenGL 편끼리의 정면 충돌이며, 지시대로 하면 화면이 깨진다.

**판정**: gl_fund_02(49행)가 옳고 gl_proj_01_cube(359행)가 틀렸다. DirectXMath 행렬은 (a) 행벡터 관례이고 (b) 행 우선 저장이다 — stage2_dx_proj_01_cube_dx11.md:328 "DirectXMath는 행 벡터 관례를 기본으로 설계됐으므로 `row_major` + `mul(v, M)` 조합이 자연스럽다"가 이를 확인한다. 이 행렬을 gl_proj_01_cube:375의 열벡터 식 `gl_Position = uProj * uView * uModel * vec4(aPos, 1.0)`에 쓰려면 필요한 수학적 행렬은 원본의 전치인데, GL_FALSE(column-major로 읽기)가 바로 그 전치를 공짜로 만들어 낸다. 검산: DirectXMath 이동 행렬의 메모리 바이트는 [1,0,0,0, 0,1,0,0, 0,0,1,0, tx,ty,tz,1]이고, GL_FALSE로 열 우선 해석하면 마지막 *열* 이 (tx,ty,tz,1)이 되어 M*v 이동이 정확히 동작한다. GL_TRUE를 주면 이동 성분이 마지막 *행* 에 놓여 M*v가 xyz를 전혀 옮기지 않고 w에 tx*x+ty*y+tz*z+1이 들어가 큐브가 붕괴한다.

**정정**: 359행을 "DirectX 측에서 가져온 row-major 행렬은 행벡터 관례이므로, 열벡터 셰이더에 쓰려면 오히려 `GL_FALSE`(column-major 해석)가 필요한 전치를 대신 만들어 준다. `GL_TRUE`를 주면 전치가 상쇄되지 않아 결과가 깨진다"로 고쳐야 gl_fund_02:49·128의 "전치가 두 번 상쇄된 우연" 경고와 일치한다.

### 2. 문서간모순 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_01_cube.md, graphics/stage0_fund_02_coordinate_transform.md, graphics/stage1_gl_fund_02_transform.md

**위치**: stage1_gl_proj_01_cube.md:675 / stage0_fund_02_coordinate_transform.md:178 / stage1_gl_fund_02_transform.md:60

```text
gl_proj_01_cube:675 — "OpenGL의 전통적 $[-1, 1]$ 깊이는 절반의 비트를 음수 표현에 낭비해 부동소수 Reverse-Z의 이점을 살리지 못한다."  ///  stage0:178 — "OpenGL의 기본 NDC 깊이 $[-1,1]$도 뷰포트 변환 뒤 깊이 버퍼의 $[0,1]$에 저장되므로, 이 범위만으로 일반적인 깊이 정밀도가 절반이 되지는 않는다."  ///  gl_fund_02:60 — "일반 깊이 테스트에서는 이 값도 `glDepthRange`를 거쳐 깊이 버퍼의 $[0,1]$로 매핑되므로 범위 차이 자체가 정밀도를 절반으로 만들지는 않는다."
```

같은 OpenGL 주제에 대해 stage0과 gl_fund_02는 "범위 차이가 비트를 반으로 깎지 않는다"를 명시적으로 못박았는데, gl_proj_01_cube는 정확히 그 반대 메커니즘("절반의 비트를 음수 표현에 낭비")을 원인으로 제시한다.

**판정**: stage0:178과 gl_fund_02:60이 옳다. 깊이 버퍼에는 음수가 저장되지 않는다 — NDC z∈[-1,1]은 뷰포트/`glDepthRange` 변환 (z+1)/2로 [0,1]에 매핑된 뒤에야 저장되므로 "음수 표현에 쓰는 비트"라는 것 자체가 존재하지 않는다. gl_proj_01_cube 자신도 581행에서 "NDC의 $z$ 좌표를 `glDepthRange`가 정한 $[0, 1]$ 범위로 매핑한 값이 저장된다"고 쓰고 있어 자기 문서 안에서도 어긋난다. float reverse-Z가 [-1,1]에서 무력해지는 진짜 이유는 비트 낭비가 아니라, (z+1)/2 아핀 재매핑이 z_ndc≈-1 부근의 성긴 float 절대 간격을 그대로 물려받아 0 근처의 조밀한 float 구간에 도달할 수 없기 때문이다.

**정정**: 675행을 "OpenGL의 전통적 $[-1,1]$ 깊이는 (z+1)/2 재매핑을 거치므로 float의 0 근처 정밀도가 reverse-Z에 활용되지 못한다"로 바꿔야 한다. "절반의 비트를 음수 표현에 낭비"는 삭제 대상이다.

### 3. 사실오류 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage3_vk_fund_01_pipeline.md, graphics/stage1_gl_fund_02_transform.md, graphics/stage0_fund_02_coordinate_transform.md

**위치**: stage3_vk_fund_01_pipeline.md:188 / stage1_gl_fund_02_transform.md:130 / stage0_fund_02_coordinate_transform.md:178

```text
vk_fund_01:188 — "OpenGL용 projection 행렬을 Vulkan에 그대로 넣으면 깊이가 절반 범위로 눌려 z-fighting이 심해진다."  ///  gl_fund_02:130 — "일반 OpenGL $[-1,1]$ 클립 규칙과 섞으면 near 클리핑과 깊이 매핑이 어긋난다."
```

GL 투영행렬을 Vulkan에 넣었을 때의 증상을 vk_fund_01은 "깊이 압축 → z-fighting 악화"로, gl_fund_02는 "near 클리핑이 어긋남"으로 서로 다르게 설명한다. 전자는 stage0:178이 명시적으로 부정한 "범위 절반 → 정밀도 절반" 추론을 API 이식 상황에 그대로 옮긴 것이기도 하다.

**판정**: gl_fund_02:130이 옳고 vk_fund_01:188이 틀렸다. Vulkan의 클립 조건은 0 ≤ z_c ≤ w_c이므로 z_ndc<0인 부분은 압축되는 게 아니라 *클리핑되어 사라진다*. 직접 계산(n=0.1, f=100): GL 행렬의 z_ndc=(f+n)/(f-n) − 2fn/((f−n)d)는 d=2fn/(f+n)=0.1998에서 0이 된다. 즉 near가 사실상 n에서 ≈2n으로 밀려 카메라 코앞 기하가 잘려나가고, 살아남은 d∈[0.1998, 100]은 여전히 깊이 [0,1] *전체* 를 쓴다(d=1→0.8018, d=10→0.9820, d=100→1.0). 깊이 범위가 절반으로 눌리는 일도, z-fighting이 심해지는 일도 없다(오히려 유효 near가 커져 far 정밀도는 미세하게 좋아진다).

**정정**: 188행을 "OpenGL용 projection 행렬을 Vulkan에 그대로 넣으면 z_ndc가 음수인 절두체 앞쪽 절반이 클립 볼륨 밖으로 판정되어 잘려 나간다(유효 near가 약 2n으로 밀린다)"로 고쳐야 한다. z-fighting 악화는 이 상황의 증상이 아니다.

### 4. 규약불일치 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_01_cube.md, graphics/stage0_fund_02_coordinate_transform.md

**위치**: stage1_gl_proj_01_cube.md:627-640 (표 헤더 633행, 값 635-640행) / 같은 파일 31, 305, 596-597행 / stage0_fund_02_coordinate_transform.md:173

```text
gl_proj_01_cube:633-635 — "| view-space z | NDC z (정규화) |" ... "| -0.1 (near) | 0.000 |", 640행 "| -100.0 (far) | 1.000 |"  ///  같은 파일 596-597 — "C[NDC<br/>z ∈ -1..1]" → "D[Viewport Transform<br/>z ∈ 0..1]"  ///  stage0:173 — "| NDC 깊이 $z_n$ 범위 | $[-1, 1]$ | $[0, 1]$ | $[0, 1]$ |"
```

표의 열 이름은 "NDC z"인데 실린 값은 near=0.000, far=1.000으로 [0,1] 범위다. 바로 위 628행의 식 $z_{NDC}=\frac{f+n}{f-n}-\frac{2fn}{(f-n)(-z_{view})}$에 n=0.1, f=100, d=0.1을 넣으면 정확히 −1.0이 나오므로, 표의 0.000은 그 식의 출력이 아니다. 같은 문서 31행("NDC ... $[-1, 1]^3$"), 305행, 596-597행이 모두 GL의 NDC z를 −1..1로 규정하고 있어, 한 문서 안에서 z_NDC라는 같은 기호가 두 범위로 쓰인다. stage0:173의 GL 행과도 어긋난다.

**판정**: 596-597행·628행·stage0:173이 옳고 633행 헤더가 틀렸다. 표의 값들은 뷰포트 변환 이후의 윈도우/깊이버퍼 값(z_win=(z_ndc+1)/2)이다. 검산: d=0.5 → z_ndc=0.6016, z_win=0.8008(표 0.801 ✓), d=1 → z_ndc=0.8018, z_win=0.9009(표 0.901 ✓). 즉 열 이름만 잘못 붙었다. 631행의 "0.1~1m 구간이 깊이 버퍼의 약 90%"라는 서술도 z_win 기준(0.901)이라 헤더가 아니라 본문이 옳다.

**정정**: 633행 헤더를 "NDC z (정규화)"에서 "깊이 버퍼 값 z_win = (z_ndc+1)/2" 또는 "윈도우 깊이 [0,1]"로 바꾼다. 헤더를 NDC로 유지하려면 값을 −1.000 / 0.602 / 0.802 / 0.962 / 0.998 / 1.000으로 바꿔야 한다.

### 5. 산술오류 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_01_cube.md

**위치**: 638, 639

```text
"| -5.0 | 0.982 |", "| -50.0 | 0.998 |"
```

위 항목에서 확정한 대로 이 표는 z_win=(z_ndc+1)/2 값이다(0.801·0.901 항목이 그 해석에서만 맞는다). 그 해석으로 재계산하면 두 행의 값이 어긋난다.

**판정**: n=0.1, f=100에서 직접 계산: d=5 → z_ndc=0.961962, z_win=0.980981 → 0.981(표는 0.982). d=50 → z_ndc=0.997998, z_win=0.998999 → 0.999(표는 0.998). 두 행 모두 반올림으로도 나올 수 없는 값이다(0.999를 0.998로 절사한 것과 0.981을 0.982로 올린 것이 서로 반대 방향이라 일관된 표기 규칙으로도 설명되지 않는다).

**정정**: -5.0 행은 0.981, -50.0 행은 0.999로 수정.

### 6. 규약불일치 (중간) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_01_cube.md, graphics/stage0_fund_02_coordinate_transform.md, graphics/stage2_dx_proj_01_cube_dx11.md

**위치**: stage1_gl_proj_01_cube.md:294 / 같은 파일 336, 375, 382 / stage0_fund_02_coordinate_transform.md:213 / stage2_dx_proj_01_cube_dx11.md:328

```text
gl_proj_01_cube:294 — "세 단계의 행렬 곱을 *M(model) × V(view) × P(projection)* 라 하고, 정점 셰이더에 uniform으로 올린다."  ///  같은 파일 336 — "*MVP = P × V × M* 으로 미리 곱한 결과 한 개만 올릴 수도 있다"  ///  stage0:213 — "$M\mathbf{v}$ 컨벤션에서는 MVP를 `P * V * M`으로 쓰고, $\mathbf{v}^T M$ 컨벤션에서는 `M * V * P`로 쓴다."
```

한 문서 안 두 절이 같은 곱을 M×V×P(294행)와 P×V×M(336행)으로 반대로 쓴다. stage0:213의 분류에 따르면 M×V×P는 행벡터($v^T M$) 관례의 표기이고, 실제로 stage2_dx_proj_01_cube_dx11.md:328이 "위 셰이더는 `mul(vector, matrix)` 순서 ... 이 관례에서 변환 합성은 `model * view * proj` 순서로 읽힌다"로 그 관례를 쓴다. 그런데 gl_proj_01_cube는 375행 `gl_Position = uProj * uView * uModel * vec4(aPos, 1.0)`으로 일관되게 열벡터 관례다.

**판정**: 336·375·382행이 옳고 294행이 틀렸다. 열벡터 관례에서 합성 행렬은 P·V·M이며, 같은 문서 382행이 "곱셈 순서를 거꾸로 `uModel * uView * uProj`로 쓰면 ... 이 순서는 컨벤션이 아니라 수학적 강제다"라고 스스로 못박고 있어, 294행의 M×V×P는 그 규칙을 위반한 표기다(DX 편의 행벡터 표기가 GL 편에 섞여 든 형태).

**정정**: 294행을 "세 단계의 행렬 곱을 P(projection) × V(view) × M(model)" 또는 곱 기호 없이 "모델·뷰·투영(MVP) 세 행렬"로 고쳐 336·375행과 맞춘다.

---

## PBR 수식 — 4건

### 1. 문서간모순 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage3_vk_proj_03_pbr.md (본문), graphics/stage0_fund_05_shading_materials.md, graphics/stage2_dx_proj_03_pbr_dx11.md

**위치**: stage3 L64, L220, L236 / stage0 L195 / stage2 L49, L223, L239

```text
stage3 L64: "Disney 관례로 `roughness*roughness`를 $\alpha$로 쓰고, 직접광 $k=(\text{rough}+1)^2/8$ / IBL $k=\alpha^2/2$로 나눈다." / stage3 L220: "IBL용 $k=\alpha^2/2$를 써야 한다." / stage3 L236(코드): "float k = (roughness*roughness)/2.0;            // IBL용 k" / stage0 L195: "직접광은 $k = (\text{roughness}+1)^2 / 8$, IBL은 $k = \text{roughness}^2 / 2$ (Karis). 두 용도의 $k$가 다른 것이 흔한 버그 지점이다." / stage2 L223: "여기서는 IBL용 $k=\text{roughness}^2/2$를 써야 한다."
```

Smith-Schlick 기하항의 IBL용 k 파라미터화를 stage3 본문만 $\alpha^2/2$로 적는다. 그런데 같은 문장(L64)과 L5에서 $\alpha=\text{roughness}^2$로 정의했으므로 $\alpha^2/2 = \text{roughness}^4/2$가 되어, stage0·stage2가 말하는 $\text{roughness}^2/2$와 값이 다르다. 게다가 stage3 자신의 LUT 코드 L236은 `(roughness*roughness)/2.0`, 즉 $\alpha/2$를 쓰고 있어 본문과 코드가 어긋난다. roughness=0.5에서 올바른 k=0.125인데 본문 식대로면 k=0.03125로, G_SchlickGGX(NoV=0.1)이 0.4706 대 0.7805로 벌어진다(직접 계산 확인).

**판정**: stage0·stage2·그리고 stage3의 코드(L236)가 옳다. stage3 본문 L64/L220의 $\alpha^2/2$가 틀렸다. Karis(UE4 course notes)의 정의는 IBL k = α/2 이고 α = roughness² 이므로 k = roughness²/2 이다. stage3가 α를 roughness²로 정의해 놓고 원문의 'k = roughness²/2'를 그대로 α로 치환해 버린 것이 원인이다.

**정정**: stage3 L64: "직접광 $k=(\text{rough}+1)^2/8$ / IBL $k=\alpha/2\;(=\text{roughness}^2/2)$로 나눈다", L220: "IBL용 $k=\alpha/2=\text{roughness}^2/2$를 써야 한다". 코드는 수정 불필요.

### 2. 문서간모순 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage3_vk_proj_03_pbr.md, graphics/stage2_dx_proj_03_pbr_dx11.md

**위치**: stage3 L88, L247, L267, L276 / stage2 L273, L279

```text
stage3 L88: "vec3 FssEss = F0 * brdf.x + brdf.y;     // 단일 산란 기여" / stage3 L247: "프레넬을 $F = F_0\cdot\text{scale} + \text{bias}$로 분해했으므로 LUT가 $F_0$에 독립이다" / stage3 L267: "vec3 F  = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NoV, 5.0);" / stage3 L276: "vec3 specular = prefiltered * (F * brdf.x + brdf.y);   // split-sum BRDF 항" / stage2 L273: "float3 specularIBL = prefilter * (F0 * envBRDF.x + envBRDF.y);   // split-sum"
```

split-sum 둘째 인수를 런타임에서 무엇과 곱하는지가 두 문서에서 다르다. stage2는 $F_0$, stage3 §6은 NoV·roughness에 의존하는 Lagarde 프레넬 $F$를 곱한다. 더욱이 stage3 안에서도 §2의 L88(FssEss = F0*brdf.x + brdf.y)과 §6의 L276(F*brdf.x + brdf.y)이 같은 자리에 서로 다른 양을 넣는다. 두 글은 서두(stage2 L3, stage3 L3)에서 "같은 수식이고 자원 바인딩만 다르다"고 명시하므로 동일 재질·동일 환경에서 결과가 갈리면 안 된다.

**판정**: stage2(그리고 stage3 §2 L88)의 $F_0$가 옳다. LUT는 Schlick 프레넬을 $F = F_0\cdot A + B$로 분해해 $F_0$를 밖으로 뺀 상태로 구운 것이며(stage3 L247이 스스로 그렇게 서술한다), B 채널이 이미 grazing 각의 프레넬 상승분을 담고 있다. 여기에 다시 각도 의존 $F$를 곱하면 프레넬이 이중 적용된다(LearnOpenGL 계열 코드의 알려진 편법이며 Karis 원전·Filament는 $F_0$를 쓴다).

**정정**: stage3 L276을 "vec3 specular = prefiltered * (F0 * brdf.x + brdf.y);"로 하거나, §2의 MultiscatterBRDF(L85~93)를 그대로 호출해 FssEss 기반으로 합성한다. L267의 roughness 반영 프레넬 $F$는 kD 계산(L268)에만 쓰는 것이 일관된다.

### 3. 규약불일치 (중간) — **[반영됨 2026-08-13]**

**파일**: graphics/stage2_dx_proj_03_pbr_dx11.md, graphics/stage3_vk_proj_03_pbr.md, graphics/stage0_fund_05_shading_materials.md

**위치**: stage2 L267-268, L96, L279 / stage3 L268, L271, L278 / stage0 L200

```text
stage2 L267-268: "float3 diffuseIBL = irradianceMap.Sample(linearSampler, N).rgb\n                        * albedo * (1.0 - metallic);" / stage2 L279: "디퓨즈에 $(1-\text{metallic})$을 곱하는 것은 직접광과 동일하다." / stage2 L96: "`kD = (1-F)(1-metallic)`이 $k_S+k_D\le 1$을 보장한다." / stage3 L268: "vec3 kD = (1.0 - F) * (1.0 - metallic);" / stage3 L278: "vec3 ambient = (kD * diffuse + specular) * ao;" / stage0 L200: "f_{\text{diff}} = (1 - F)(1 - \text{metallic})\frac{c_{\text{albedo}}}{\pi}"
```

환경광(IBL) 디퓨즈의 에너지 분배 규약이 갈린다. stage3는 $k_D=(1-F)(1-\text{metallic})$을 곱하고, stage2는 $(1-\text{metallic})$만 곱해 $(1-F)$ 항을 뺀다. stage2 L279는 "직접광과 동일하다"고 적지만 stage2의 직접광 kD(L89)는 $(1-F)(1-\text{metallic})$이라 실제로는 동일하지 않다. 그 결과 같은 재질·환경에서 두 글의 앰비언트 디퓨즈가 유전체 기준 grazing 각에서 눈에 띄게 달라지고, stage2 IBL 경로는 스스로 L96에서 내건 $k_S+k_D\le1$ 보장을 만족하지 않는다.

**판정**: 시리즈 자체 기준으로는 stage0 L200과 stage3가 정합적이다(에너지 보존 규약을 $k_D=(1-F)(1-\text{metallic})$으로 선언했으므로 IBL에서도 같은 규약을 지켜야 한다). stage2의 형태도 Filament/Karis에서 쓰이는 근사이지만, 같은 시리즈가 "수식은 동일"이라고 못 박은 이상 한쪽으로 통일해야 하고, 통일 기준은 stage0이 이미 선언한 $(1-F)(1-\text{metallic})$이다.

**정정**: stage2 L267-268을 "float3 kD = (1.0 - F_roughness(NoV, F0, roughness)) * (1.0 - metallic); float3 diffuseIBL = irradianceMap.Sample(linearSampler, N).rgb * albedo * kD;" 형태로 맞추거나, 최소한 L279에 "IBL 디퓨즈는 각도 평균이 없어 $(1-F)$를 생략하는 근사를 택했다"고 명시해 직접광 kD와 다름을 밝혀야 한다.

### 4. 코드오류 (중간) — **[반영됨 2026-08-13]**

**파일**: graphics/stage3_vk_proj_03_pbr.md

**위치**: L134-138, L194, L209-212

```text
L136: "cubeInfo.mipLevels = 1; cubeInfo.arrayLayers = 6;   // 6 면" / L138: "cubeInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;" / L194: "Vulkan 구현의 요점은 *Filtered Importance Sampling* — 샘플 입체각과 텍셀 입체각을 비교해 mip을 골라 거친 표면의 반딧불이 노이즈를 억제한다(`textureLod`)." / L212: "prefiltered += textureLod(envMap, L, mip).rgb * NoL;"
```

4절이 만드는 환경 큐브맵은 `mipLevels = 1`이고 usage에 TRANSFER_SRC/DST가 없어 밉 체인을 생성할 수도 없다. 그런데 5절 프리필터 셰이더는 바로 그 `envMap`을 `textureLod(envMap, L, mip)`으로 샘플하며 mip>0을 골라 반딧불이를 억제한다고 설명한다. 밉이 1레벨뿐이면 LOD가 레벨 0으로 클램프되어 FIS가 아무 효과도 내지 못하고, 결과는 stage2의 `SampleLevel(..., L, 0)`과 같아진다 — 즉 문서가 'Vulkan 구현의 요점'이라고 내세운 기법이 제시된 코드로는 동작하지 않는다.

**판정**: 5절의 FIS 설명이 옳고 4절의 이미지 생성 파라미터가 부족하다. FIS는 원본 환경 큐브맵의 밉 체인을 전제하는 기법이다(saTexel을 mip0 텍셀 입체각으로 잡고 saSample과 비교해 상위 밉을 고르는 구조).

**정정**: 4절 큐브맵을 `mipLevels = floor(log2(512))+1 = 10`으로 만들고 usage에 `VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`를 추가한 뒤 `vkCmdBlitImage` 밉 생성을 거쳐야 5절의 `textureLod` FIS가 실제로 동작한다(메모리 계산 12MB도 밉 포함 약 16MB로 갱신).

---

## 감마 · 색공간 — 3건

### 1. 문서간모순 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_fund_04_texture.md , graphics/stage0_fund_07_texturing_sampling.md

**위치**: stage1_gl_fund_04_texture.md L599 ↔ 같은 파일 L365 / stage0_fund_07_texturing_sampling.md L118

```text
stage1_gl_fund_04_texture.md L599: "중요한 점은 *변환이 텍스처 캐시 이후, 필터링 이전* 에 일어난다는 것이다. 즉 bilinear 보간과 밉맵 다운샘플의 평균은 *Linear 공간에서* 수행된다. 이는 색 공간 관점에서 올바른 동작이다."

stage1_gl_fund_04_texture.md L365: "- *sRGB 디퓨즈*: sRGB 코드값을 그대로 평균하면 선형 광량 평균보다 어두워진다. 선형 공간으로 디코드해 필터링하고 결과를 다시 sRGB로 인코드한다."

stage0_fund_07_texturing_sampling.md L118: "한 가지 함정: 밉맵 *생성* 의 평균은 색 텍스처라면 선형 빛에서 해야 한다. sRGB 코드값을 그대로 평균해 밉을 만들면 멀리 있는 고대비 텍스처가 어두워진다. sRGB 샘플링의 자동 디코딩과 밉 생성은 별개 계약이며, 런타임 생성 명령의 필터링 규칙과 포맷 지원도 API별로 확인해야 한다."
```

같은 OpenGL 편(stage1_gl_fund_04) 안에서 "밉맵 다운샘플의 평균"이 선형 공간에서 일어나는지에 대해 두 절이 정반대로 말한다. L599(8절)는 sRGB 하드웨어 경로의 이점으로 "밉맵 다운샘플의 평균은 Linear 공간에서 수행된다"고 단정하고, L365(3절 자동 밉맵 생성)는 정확히 그 반대로 `glGenerateMipmap`이 sRGB 디퓨즈에 "부적절하다"며 코드값을 그대로 평균해 어두워지므로 직접 디코드→필터→인코드하라고 한다. API 중립 기초편 stage0_fund_07 L118은 "sRGB 샘플링의 자동 디코딩과 밉 생성은 별개 계약"이라고 명시해 L365 편을 든다. 게다가 stage1_gl_fund_04의 실제 예제 코드(L772~776)는 `GL_SRGB8_ALPHA8`로 적재한 뒤 곧바로 `glGenerateMipmap`을 부르는데, L599를 믿으면 이 코드가 안전해 보이고 L365를 믿으면 부적절한 코드가 된다.

**판정**: L365 / stage0_fund_07 L118 쪽이 옳다. L599의 "밉맵 다운샘플의 평균" 부분이 틀렸다.

**정정**: OpenGL이 보장하는 것은 *샘플러 경로*의 계약이다 — sRGB 디코딩이 필터링 이전에 적용되므로 bilinear 4-탭 가중평균과 trilinear의 두 밉 레벨 간 보간은 선형 공간에서 일어난다. 반면 밉 체인을 *만드는* 축소(`glGenerateMipmap`)는 샘플러 경로가 아니라 별개의 연산이고, 명세는 필터 알고리즘을 구현에 맡기며 sRGB 디코드 후 평균한다는 보장을 주지 않는다(드라이버마다 다름). 따라서 L599는 "bilinear/trilinear 보간(즉 두 밉 레벨 사이의 보간 포함)은 Linear 공간에서 수행된다"로 한정해야 하고, 밉 *생성*은 L365·stage0_fund_07 L118대로 별개 계약(오프라인에서 디코드→필터→인코드)임을 그대로 두어야 한다.

### 2. 문서간모순 (중간) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_05_postprocess.md , graphics/stage1_gl_fund_04_texture.md

**위치**: stage1_gl_proj_05_postprocess.md L569, L571 ↔ stage1_gl_fund_04_texture.md L621

```text
stage1_gl_proj_05_postprocess.md L569: "OpenGL은 `glEnable(GL_FRAMEBUFFER_SRGB)`로 *하드웨어 sRGB 변환*을 켤 수 있다. 이 모드에서는 프레임버퍼에 기록하는 선형 값이 자동으로 sRGB로 인코딩된다."

stage1_gl_proj_05_postprocess.md L571: "아니면 `GL_FRAMEBUFFER_SRGB`를 켜고 셰이더의 `pow`를 제거한다. 둘 다 sRGB 변환을 정확히 한 번만 적용하는 동일한 결과를 낸다."

stage1_gl_fund_04_texture.md L621: "이걸 켜면 *프레임버퍼가 sRGB 포맷일 때만* 자동 변환이 적용된다. 기본 프레임버퍼의 픽셀 포맷도 GLFW 힌트만으로는 충분하지 않고 드라이버·컴포지터가 실제 sRGB 포맷을 잡아 줘야 한다. 디버그 콜백으로 `GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING`을 조회해 `GL_SRGB`인지 확인하는 절차를 권한다."
```

같은 GL 트랙의 두 편이 `GL_FRAMEBUFFER_SRGB`의 계약을 다르게 서술한다. stage1_gl_fund_04 L621은 "프레임버퍼가 sRGB 포맷일 때만"이라는 조건을 못 박고, 기본 프레임버퍼는 GLFW 힌트(`GLFW_SRGB_CAPABLE`)만으로도 보장되지 않으니 `GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING`으로 확인하라고까지 한다. 반면 stage1_gl_proj_05 L569/L571은 그 조건을 전제하지 않고 enable만으로 자동 인코딩이 켜진다고 쓴 뒤, 두 경로가 "동일한 결과를 낸다"고 무조건 단정한다. stage1_gl_proj_05 전문에 `GLFW_SRGB_CAPABLE`이나 첨부 인코딩 확인이 한 번도 나오지 않으므로(전 파일 검색 확인), 독자가 L571의 두 번째 경로(FRAMEBUFFER_SRGB 켜고 pow 제거)를 그대로 따르면 기본 FB가 sRGB가 아닌 환경에서 인코딩이 통째로 빠져 화면이 어두워진다. 이는 stage0_fund_04 L98이 말한 "출력 인코딩 누락 → 전체적으로 어두워진다" 증상과 정확히 같다.

**판정**: stage1_gl_fund_04 L621이 옳고, stage1_gl_proj_05 L571의 무조건적 동치 주장이 부정확하다.

**정정**: `glEnable(GL_FRAMEBUFFER_SRGB)`는 "현재 그리는 첨부의 color encoding이 GL_SRGB일 때만" 선형→sRGB 인코딩을 수행하는 스위치다(기본 프레임버퍼는 sRGB-capable 픽셀 포맷을 실제로 받아야 하고, FBO는 첨부가 `GL_SRGB8_ALPHA8`이어야 한다). 따라서 L571은 "기본 프레임버퍼가 sRGB-capable로 잡혔음을 `GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING`으로 확인한 경우에 한해 두 경로가 같은 결과를 낸다"로 조건을 붙여야 한다.

### 3. 산술오류 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_proj_05_postprocess.md

**위치**: L399 (식 L388~390, 코드 L392~397) ↔ L382, L470, L473

```text
L399: "`lWhite`는 완전한 흰색으로 매핑하고 싶은 입력 휘도다. 그 이상은 모두 1.0이 된다."

L382 (기본 Reinhard): "입력이 0이면 0, 무한대로 가면 1에 점근한다. 어떤 큰 값도 1을 넘지 않으므로 clamp가 필요 없다."

L473 (ACES): "`clamp`가 마지막에 붙는 주된 이유는 정상적인 큰 HDR 입력에서도 함수가 $a/c\approx1.03$으로 수렴해 1을 넘기 때문이다."
```

Reinhard Extended는 lWhite를 넘는 입력에서 1로 포화하지 않고 계속 증가한다. 문서에 적힌 식 $L_{out}=L_{in}(1+L_{in}/L_{white}^2)/(1+L_{in})$을 직접 계산하면 $L_{in}=L_{white}$에서 정확히 1이고 그 이후로는 1을 넘어 발산한다. lWhite=4에서 입력 6 → 1.179, 16 → 1.882, 80 → 5.926 (스크립트로 재현). 그런데 L392~397의 `ReinhardExt` 코드에는 clamp가 없다. 같은 절에서 문서는 기본 Reinhard에 대해서는 "1을 넘지 않으므로 clamp가 필요 없다"(L382)고 근거를 대고, ACES에 대해서는 1.03으로 수렴해 1을 넘으니 clamp가 필요하다(L470·L473)고 명시적으로 짚는다. 즉 오버슈트 여부를 다른 두 함수에서는 정확히 따졌는데 Reinhard Extended에서만 반대 방향으로 틀렸다. 또 L366은 톤 매핑을 "0에서 1로 압축하는 단조 증가 함수"로 정의해 놓아, 연산자 자체가 [0,1]을 벗어난다는 사실이 그 정의와도 어긋난다.

**판정**: L399의 "그 이상은 모두 1.0이 된다"가 틀렸다. L382·L473의 오버슈트 판정 기준이 옳고, 그 기준을 Reinhard Extended에 적용하면 clamp가 필요하다.

**정정**: "lWhite에서 출력이 정확히 1.0이 되고, 그보다 큰 입력에서는 1을 넘어 계속 증가하므로 ACESFilm처럼 마지막에 `clamp(..., 0.0, 1.0)`가 필요하다(LDR 프레임버퍼에 쓰면 하드웨어가 잘라 주지만, HDR 중간 버퍼로 넘길 때는 명시적으로 잘라야 한다)"로 고쳐야 하고, `ReinhardExt` 코드에 clamp를 추가하는 편이 L382·L473의 서술과 일관된다.

---

## 동기화 · 배리어 · 리소스 상태 — 2건

### 1. 문서간모순 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage3_vk_fund_02_vulkan_core.md

**위치**: L365-366 (6절 「프레임 루프」) ↔ L517-522, L538 (9절 「한 프레임의 명령 흐름」)

```text
6절 L365-366:
```
vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
vkResetFences(device, 1, &inFlightFence);

vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                      imageAvailable, VK_NULL_HANDLE, &imageIndex);
```

9절 L517-522:
```
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                       imageAvailable[frame],
                                       VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return; }

    vkResetFences(device, 1, &inFlight[frame]);
```

9절 L538 (본문):
「`vkResetFences`를 acquire 결과를 확인한 *뒤에* 부르는 순서가 중요하다. acquire가 `OUT_OF_DATE`로 일찍 반환하면 submit을 건너뛰는데, 그 전에 fence를 reset해 버리면 어떤 작업도 그 fence를 다시 신호하지 않아 다음 프레임의 `vkWaitForFences`가 영원히 막힌다.」
```

같은 문서가 프레임 루프를 두 번 제시하는데 `vkResetFences`의 위치가 정반대다. 6절 코드는 wait 직후·acquire 이전에 fence를 reset한다. 9절은 acquire 결과 확인 뒤에 reset하고, 본문 L538에서 6절이 쓴 바로 그 순서를 '영원히 막히는' 교착 함정으로 명시적으로 지목한다. 즉 문서가 스스로 함정이라고 부른 패턴을 앞 절 코드가 그대로 쓰고 있다. 6절 코드는 acquire 반환값을 받지도 않아, 독자가 이 골격에 `OUT_OF_DATE` 처리를 얹는 순간(9절이 시키는 대로) 곧바로 교착에 빠진다.

**판정**: 9절(L517-522 + L538)이 옳다. 6절 L366의 위치가 잘못됐다.

**정정**: Vulkan에서 fence는 unsignaled 상태로 reset된 뒤 오직 `vkQueueSubmit`의 fence 인자로만 다시 signal된다. acquire가 `VK_ERROR_OUT_OF_DATE_KHR`로 반환해 submit을 건너뛰면 그 fence를 signal할 주체가 없어지고, 다음 프레임의 `vkWaitForFences(..., UINT64_MAX)`가 무한 대기한다. 따라서 reset은 '이번 프레임에 반드시 submit한다'가 확정된 뒤 — 즉 acquire 결과를 확인한 뒤 — 해야 한다. 6절 코드를 다음처럼 고쳐 9절과 맞춘다.
```
vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                   imageAvailable, VK_NULL_HANDLE, &imageIndex);
if (r == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return; }

vkResetFences(device, 1, &inFlightFence);   // submit이 확정된 뒤에 reset
```

### 2. 문서간모순 (확정) — **[반영됨 2026-08-13]**

**파일**: graphics/stage2_dx_proj_04_dx12_transition.md (참고 대조: graphics/stage2_dx_fund_04_dx12.md)

**위치**: stage2_dx_proj_04_dx12_transition.md L509 ↔ L520 (둘 다 「다중 Barrier 통합」 절, 코드블록 하나를 사이에 둠) / 참고: stage2_dx_fund_04_dx12.md L130

```text
L509: 「서로 같은 지점에서 필요한 배리어는 한 `ResourceBarrier` 호출의 배열로 묶어 CPU 호출 비용을 줄이는 것이 권장된다. API는 배열 원소를 주어진 순서대로 개별 호출한 것처럼 해석하며, 캐시 플러시가 반드시 한 번으로 합쳐진다고 보장하지 않는다.」

L520: 「세 개를 따로 발급하면 GPU가 세 번 동기화 지점을 만들지만, 묶으면 한 번의 장벽으로 처리된다.」

(대조) stage2_dx_fund_04_dx12.md L130: 「같은 시점에 전환할 자원이 여럿이면 배열로 모아 한 번에 넘기는 편이 드라이버가 동기화를 묶어 처리할 여지를 준다.」
```

같은 절 안에서 11줄 간격으로 정반대 주장을 한다. L509는 배열 묶음의 이득을 'CPU 호출 비용'으로 한정하고 '캐시 플러시가 한 번으로 합쳐진다고 보장하지 않는다'고 못 박는데, L520은 '묶으면 한 번의 장벽으로 처리된다'고 GPU 측 병합을 단정한다. 같은 시리즈의 stage2_dx_fund_04 L130은 '묶어 처리할 여지를 준다'로 신중하게 썼고, Vulkan 편(stage3_vk_proj_05_compute.md L301)도 같은 종류의 단정을 '모든 구현에서 …물리적 실행을 단정할 수는 없다'며 명시적으로 피한다. L520만 시리즈의 다른 서술과 어긋난다.

**판정**: L509가 옳다. L520의 '묶으면 한 번의 장벽으로 처리된다'가 틀렸다.

**정정**: D3D12 `ResourceBarrier`는 배열로 넘긴 배리어들을 '주어진 순서대로 하나씩 호출한 것과 동일'하게 정의할 뿐, 런타임/드라이버가 이를 단일 캐시 플러시나 단일 동기화 지점으로 병합한다는 보장은 명세에 없다. 배칭이 권장되는 근거는 (a) API 호출 오버헤드 감소와 (b) 드라이버가 병합할 '기회'를 주는 것이지, 병합의 보증이 아니다. L520을 예컨대 「세 개를 따로 발급하면 드라이버가 각각을 별도 동기화 지점으로 처리할 수 있고, 한 호출로 묶으면 드라이버가 이를 합쳐 처리할 여지가 생긴다」처럼 L509·fund_04 L130과 같은 강도로 낮춰야 한다.

---

## GPU 아키텍처 · 래스터화 · 깊이 — 4건

### 1. 문서간모순 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage0_fund_03_rasterization.md ↔ graphics/stage0_fund_06_depth_visibility.md

**위치**: stage0_fund_03 L208 ↔ stage0_fund_06 L90

```text
stage0_fund_03 L208: "부동소수점 깊이 버퍼는 가수가 0 근처에 몰리므로 $1-z_d$를 저장하는 *reverse-Z* 로 float의 조밀한 구간을 far로 보내면, float의 로그형 간격과 $1/z$의 쌍곡 간격이 거의 상쇄돼 전 구간 정밀도가 균일해진다."

stage0_fund_06 L90: "“거의 균등”은 정확한 표현이 아니다. 정밀도는 여전히 거리와 형식에 의존하지만, 32비트 float reverse-Z는 넓은 거리 범위에서 뷰 공간 상대 정밀도를 훨씬 고르게 만든다."
```

stage0_fund_06이 reverse-Z 서술에서 "거의 균등"이라는 표현을 **명시적으로 함정으로 지목해 부정**하고 있는데, 같은 stage0 기초 트랙의 stage0_fund_03은 그보다 더 강한 표현("거의 상쇄돼 **전 구간** 정밀도가 **균일해진다**")을 그대로 쓰고 있다. 두 편은 같은 API 중립 기초 트랙이고 같은 대상(float 깊이 버퍼 + reverse-Z)을 말하므로 API 규약 차이로 설명되지 않는 정면 충돌이다. 직전 math↔physics 대조에서 나온 "한 문서가 지목한 함정을 다른 문서가 그대로 쓰는" 유형과 동일하다.

**판정**: stage0_fund_06 L90이 옳다. float32 + reverse-Z에서 두 비선형성이 상당히 상쇄되는 것은 맞지만 '균일'하지는 않다. 정밀도는 여전히 n·f·거리·형식에 의존하며, 상쇄가 좋아지는 것은 절대 간격이 아니라 뷰 공간 **상대** 정밀도이고 그것도 완전 상수가 아니다. 또 stage0_fund_06 L90이 이어서 지적하듯 D24 같은 고정소수점 형식에서는 매핑을 뒤집어도 표현 가능한 깊이 평면 수가 늘지 않으므로 '균일'은 형식을 가리지 않는 서술로 읽힐 위험까지 있다. 두 편이 공통으로 인용하는 Nathan Reed "Depth Precision Visualized"도 '거의 상쇄(nearly cancel)'까지만 주장한다.

**정정**: stage0_fund_03 L208의 마지막 절을 stage0_fund_06 L90과 같은 수위로 낮춘다. 예: "float의 로그형 간격과 $1/z$의 쌍곡 간격이 서로를 상당히 상쇄해, 넓은 거리 범위에서 뷰 공간 **상대** 정밀도가 훨씬 고르게 된다(완전히 균일해지는 것은 아니며, 이 이득은 부동소수점 형식에서만 나온다)."

### 2. 사실오류 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage1_gl_fund_05_framebuffer.md (참조: graphics/stage0_fund_06_depth_visibility.md)

**위치**: stage1_gl_fund_05 L258 (관련 L17, L241, L256) ↔ stage0_fund_06 L90

```text
stage1_gl_fund_05 L258: "Reverse-Z를 켜려면 세 가지를 함께 바꿔야 한다. 투영 행렬을 near↔far가 뒤집힌 형태로 구성하고, 깊이 비교 함수를 `glDepthFunc(GL_GREATER)`로 바꾸고, 깊이 클리어 값을 `glClearDepth(0.0)`으로 바꾼다. … 추가로, NDC 깊이 범위를 $[0,1]$로 두는 `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`(4.5+)를 함께 쓰면 부동소수점 정밀도를 더 살릴 수 있다."

stage1_gl_fund_05 L256: "먼 곳은 0 근처 float 정밀도를 받으므로 광역 야외 장면의 Z-fighting이 크게 줄어든다."

stage0_fund_06 L90: "reverse-Z의 큰 이득은 **부동소수점 깊이 형식과 결합할 때** 나온다."
```

`glClipControl(GL_ZERO_TO_ONE)`을 "추가로 … 더 살릴 수 있다"는 선택적 보너스로 적었지만, OpenGL 기본 규약에서는 이것이 **사실상 필수**다. GL 기본 NDC는 $z_{ndc}\in[-1,1]$이고 윈도 깊이는 $z_w = 0.5\,z_{ndc}+0.5$로 변환되어 깊이 버퍼에 저장된다. reverse-Z에서 far는 $z_{ndc}\approx-1$ 부근인데, float32의 ulp는 크기 1 근방($[0.5,1)$)에서 $2^{-24}\approx5.96\times10^{-8}$이므로 $z_w=0.5(z_{ndc}+1)$의 구분 가능한 최소 간격은 $\approx3\times10^{-8}$에서 멈춘다. 즉 far 쪽에서 얻는 분해능이 24비트 unorm($5.96\times10^{-8}$)의 겨우 2배 수준으로, float 형식이 0 근처에서 제공해야 할 수십 자릿수의 여유가 $[-1,1]\to[0,1]$ 중간 변환에서 통째로 소실된다. 그런데 같은 문서 L241의 표는 `GL_DEPTH_COMPONENT32F`의 용도를 "Reverse-Z, 광역 장면"이라 적고 L256은 "Z-fighting이 크게 줄어든다"고 단언한다. 게다가 L17이 밝힌 이 글의 기준 컨텍스트는 **OpenGL 3.3 Core**여서 `glClipControl`(4.5+) 자체가 없다 — 기준 컨텍스트 독자는 L258의 3단계를 그대로 따라도 L256이 약속한 이득을 거의 못 받는다. D3D·Vulkan은 $[0,1]$ 클립 깊이가 기본이라 이 함정이 없으므로(stage0_fund_06 L92), 이는 "다른 API의 전제를 GL 편에 그대로 옮긴" 형태의 오류다.

**판정**: stage1_gl_fund_05 L258의 서술이 틀렸다(과소 진술). stage0_fund_06 L90의 "reverse-Z의 큰 이득은 부동소수점 깊이 형식과 결합할 때 나온다"가 성립하려면 GL에서는 클립 깊이 범위를 $[0,1]$로 바꾸는 것이 전제 조건이다. 두 편이 공통 참고문헌으로 든 Nathan Reed "Depth Precision Visualized"도 float reverse-Z의 조건으로 클립 공간 깊이 $[0,1]$을 명시한다.

**정정**: "Reverse-Z를 켜려면 **네 가지**를 함께 바꿔야 한다"로 고치고 `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`(4.5+)을 선택 항목이 아니라 필수 항목으로 올린다. 덧붙일 문장 예: "GL 기본 NDC $[-1,1]$을 그대로 두면 윈도 깊이가 $0.5z_{ndc}+0.5$로 계산되면서 far 쪽 분해능이 약 $3\times10^{-8}$에서 막혀 24비트 unorm과 별 차이가 없다. 즉 32F reverse-Z의 이득은 클립 깊이를 $[0,1]$로 둘 때만 나온다." 또한 기준 컨텍스트가 GL 3.3 Core이므로 L241 표의 `GL_DEPTH_COMPONENT32F` 용도 칸과 L256에도 "4.5+ `glClipControl` 필요" 단서를 달아야 한다.

### 3. 규약불일치 (높음) — **[반영됨 2026-08-13]**

**파일**: graphics/stage0_adv_02_compute_gpgpu.md ↔ graphics/stage0_adv_01_gpu_architecture.md, graphics/stage2_dx_proj_05_compute_dxr.md

**위치**: stage0_adv_02 L164 ↔ stage0_adv_01 L51, L72, L88 / stage2_dx_proj_05 L145 (또한 stage0_adv_02 자체의 L79와 충돌)

```text
stage0_adv_02 L164: "**발산(Divergence)**: 같은 워프(32스레드)의 스레드들은 *명령을 함께(lockstep)* 실행한다. 그런데 `if` 분기에서 일부는 참, 일부는 거짓이면 — 하드웨어는 *양쪽 경로를 모두* 실행하고 해당 안 되는 스레드를 마스킹한다."

stage0_adv_01 L51: "“warp 하나 = 폭 32 ALU 하나 = 32비트 마스크 하나”는 NVIDIA의 특정 세대를 단순화한 그림일 뿐 GPU 일반 계약이 아니다."
stage0_adv_01 L72: "subgroup 크기는 벤더 이름만으로 32 또는 64라고 고정할 수 없고, Vulkan에서는 장치 속성과 subgroup size control 기능을 질의해야 한다."
stage0_adv_01 L45: "Volta 이후에는 lane별 프로그램 카운터·호출 스택을 유지하는 Independent Thread Scheduling을 사용한다."
stage2_dx_proj_05 L145: "다만 Wave 크기는 벤더·세대마다 다르므로 32 또는 64를 가정한 코드는 이식성이 떨어진다."

stage0_adv_02 L79(같은 문서 §2): "실제 subgroup 크기의 배수면 마지막 subgroup의 빈 lane을 줄일 수 있지만 … 장치가 보고한 한도와 subgroup 속성을 확인하고"
```

실행 묶음 폭 규약이 편마다 어긋난다. stage0_adv_01(L34·L290 표)과 stage2_dx_proj_05(L128)은 NVIDIA 32 / AMD wave32·wave64 / Intel SIMD8·16·32을 정확히 병기하고 두 편 모두 "32로 고정 가정하지 말라"고 못 박는데, stage0_adv_02 L164만 API 중립 본문에서 "워프(**32스레드**)"를 일반 실행 묶음의 정의처럼 단정한다. 이는 stage0_adv_01 L51이 "GPU 일반 계약이 아니다"라고 명시적으로 지목한 바로 그 단순화이고, stage0_adv_01 L88의 "항상 1/32는 아니다"와도 어긋난다. 더구나 같은 문서 §2 L79는 subgroup 크기를 질의해서 쓰라고 하므로 **한 문서 안에서 두 절이 서로 다른 폭 규약**을 쓰는 상태다. 같은 문장의 "lockstep"·"하드웨어는 양쪽 경로를 모두 실행하고"도 stage0_adv_01 L45(Volta+ Independent Thread Scheduling)·L192("짧은 분기는 predication으로 바뀔 수 있고")·L202와 충돌한다.

**판정**: stage0_adv_01·stage2_dx_proj_05가 옳다. 32은 NVIDIA warp의 값일 뿐 실행 묶음 폭의 일반값이 아니며(AMD RDNA는 wave32/wave64, GCN은 wave64, Intel Xe는 SIMD8/16/32), Volta 이후 NVIDIA는 lane별 PC를 유지해 엄밀한 lockstep도 아니다. stage0_adv_02 §6만 이 규약에서 이탈해 있다.

**정정**: stage0_adv_02 L164를 "같은 실행 묶음(warp/wave/subgroup — 폭은 구현마다 32, 64 등으로 다르다)의 스레드들은 같은 명령을 함께 발행받는다. `if` 분기에서 lane마다 조건이 갈리면 하드웨어는 활성 마스크를 달리해 여러 경로를 처리해야 하므로 두 경로 비용이 모두 든다" 형태로 고쳐 §2 L79 및 stage0_adv_01의 규약과 맞춘다.

### 4. 문서간모순 (중간) — **[반영됨 2026-08-13]**

**파일**: graphics/stage0_adv_02_compute_gpgpu.md ↔ graphics/stage0_adv_01_gpu_architecture.md, graphics/stage0_fund_03_rasterization.md

**위치**: stage0_adv_02 L164 ↔ stage0_adv_01 L268

```text
stage0_adv_02 L164(문장 끝): "그래서 컴퓨트 커널은 *워프 안 스레드가 같은 분기를 타도록* 데이터를 정렬하거나 분기를 줄이는 게 중요하다. **픽셀 2×2 쿼드도 같은 이유로 인접 픽셀을 묶는다.**"

stage0_adv_01 L268: "프래그먼트 셰이더는 **화면 도함수를 만들 수 있도록** 보통 *2×2 invocation quad*로 조직된다. 삼각형이 한 샘플만 덮더라도 **도함수 계산에 필요한** 이웃 lane이 helper invocation으로 셰이더 명령 일부를 실행할 수 있지만…"
```

같은 하드웨어 구조(2×2 프래그먼트 쿼드)의 **존재 이유**를 두 편이 다르게 설명한다. stage0_adv_02는 앞 문장의 "발산을 줄이려고 같은 분기를 타는 lane을 모은다"를 받아 "같은 이유로" 쿼드가 인접 픽셀을 묶는다고 쓴다. 하지만 2×2 쿼드는 분기 일관성을 위해 도입된 구조가 아니라 `ddx`/`ddy`(및 그것에 의존하는 밉 LOD 선택)를 이웃 lane 간 유한차분으로 계산하기 위한 구조다. 실제로 쿼드가 발산 감소용이라면 커버되지 않은 lane까지 helper invocation으로 실행시키는(=오히려 유효 셰이딩 효율을 떨어뜨리는) stage0_adv_01 L268~270의 quad overshading 설명이 성립하지 않는다. stage0_fund_03 L261도 쿼드를 GPU의 처리 단위로만 언급할 뿐 발산 감소를 이유로 들지 않는다.

**판정**: stage0_adv_01 L268이 옳다. 2×2 쿼드의 존재 이유는 화면 공간 도함수 계산이며, 인접 픽셀이 같은 묶음에 들어가 분기 일관성이 좋아지는 것은 결과적 부수 효과지 원인이 아니다. 참고로 stage0_adv_01 L235도 화면 공간 인접 픽셀의 분기 일관성은 쿼드가 아니라 subgroup 묶임 차원에서 따로 설명한다.

**정정**: stage0_adv_02 L164 마지막 문장을 "픽셀 2×2 쿼드는 원래 화면 도함수(`ddx`/`ddy`, 밉 LOD) 계산을 위한 묶음이고, 그 결과 인접 픽셀이 같은 묶음에 들어가 분기 일관성도 좋아지는 편이다"로 고친다(또는 발산 절에서 빼고 도함수 설명으로 옮긴다).

---

## 대조했으나 모순이 없던 항목

**NDC · 좌표계 규약** (6건 발견)

7개 문서에서 NDC·좌표계 관련 절만 발췌 대조. (1) 깊이 범위: stage0:172-173 표, gl_fund_02:60, gl_proj_01_cube:31/305/596-597/675, dx_proj_01:156, vk_fund_01:182-186, vk_proj_01:178 — GL [-1,1] / D3D·Vulkan [0,1] 배정은 전부 일치, 자기 API 규약을 남의 API에 적용한 사례 없음. (2) Vulkan Y축: stage0:174/180(양수 viewport 높이에서 +Y가 아래, 투영 Y스케일 반전 또는 음수 높이 뷰포트), vk_fund_01:188(m11 부호 반전 또는 음수 height, VK_KHR_maintenance1), vk_proj_01:180-191(GLM_FORCE_DEPTH_ZERO_TO_ONE + proj[1][1]*=-1) — 대처법·확장 이름 모두 일관. (3) 손잡이: stage0:176/182, dx_proj_02:117/576(SimpleMath는 XNA 호환 오른손이라 ConvertToLeftHanded와 어긋난다는 경고가 이미 본문에 명시됨), gl_proj_02:112-114 — 확정된 SimpleMath 오해를 공유하는 다른 DX 편은 없음(SimpleMath 언급 자체가 dx_proj_02 한 곳뿐, dx_proj_01은 XMMatrixPerspectiveFovLH만 언급). (4) 곱셈 순서·메모리 레이아웃: stage0:211-215, gl_fund_02:43-54, gl_proj_01_cube:359/375, dx_proj_01:285-287/326/328/345 — HLSL cbuffer 기본 column-major 패킹, DirectXMath 행벡터 mul(v,M), GLM 열벡터 P*V*M 배정 자체는 일치. (5) 투영행렬 유도: stage0:112-131의 GL식 3행/4행 부호를 직접 검산(-(f+n)/(f-n), -2fn/(f-n), 마지막 행 (0,0,-1,0)) 및 154-163 수치 예제 전부 재계산(NDC (0.4,0.2,0.76)/(0.2,0.1,0.98), 화면 x=1344) — 모두 정확. (6) gl_proj_01_cube:628의 z_NDC 식과 631의 \"90%\" 주장도 재계산해 정확함을 확인. (7) stage2_dx_fund_04_dx12.md는 PSO·배리어·디스크립터 힙 중심이라 NDC/좌표계 서술이 사실상 없어 대조 대상에서 제외.

**PBR 수식** (4건 발견)

네 문서에서 PBR 수식을 다루는 구간만 대조했다. stage1(GL 라이팅)은 Blinn-Phong 전용이라 PBR 수식이 없어(문서 전체에 GGX/Smith/Schlick/metallic 문자열 0건, L641에서 PBR을 후속 주제로만 예고) 직접 대조 대상이 아니었다. 실제로 성분별로 맞춰 본 항목과 결과: (1) D_GGX — stage0 L184 식과 stage2 L27-33, stage3 L67-72 코드가 완전 일치, 분모 제곱·α=roughness² 모두 동일. (2) Smith G의 분리형과 G_1 형태 — stage0 L192 = stage2 L35-37 = stage3 L73, 동일. (3) 직접광 k=(roughness+1)²/8 — 세 문서 본문·코드 모두 일치(모순 없음). (4) IBL k — 불일치, finding 1. (5) Cook-Torrance 분모 4(N·L)(N·V) — stage0 L200, stage2 L5/L66, stage3 L5 모두 일치. (6) Fresnel-Schlick — 세 문서 동일, stage3 L267의 Lagarde 변형은 IBL 전용으로 별도 명시돼 있어 모순 아님. (7) Lambert 1/π — stage0 L200과 stage2 L90 일치. (8) 조도 맵의 π 정규화 — stage2 L172와 stage3 L188이 동일 관례(π×평균)이고, cosθ·sinθ 가중도 같아 런타임에서 /π 없이 albedo를 곱하는 것과 정합함(리만합으로 직접 확인). (9) 지시받은 기확정 항목 재확인 — stage3 L237은 현재 `GeometrySchlickGGX(NoV,k)`로 uv.x 기반 NoV를 쓰고 있어 N.z 문제가 없다(이미 수정된 상태로 보임). 같은 자리인 stage2 L240도 `G_SchlickGGX(NoV, k)`로 tid.x 기반 NoV를 쓰고, 직접광 경로(stage2 L38-42, stage3 L74-78)도 dot(N,V)를 정상 사용해 동일 결함 없음. (10) reflect(-V,H)와 normalize(2·dot(V,H)·H−V)가 수학적으로 같음을 전개해 확인. (11) 프리필터 pdf = D·NoH/(4·VoH), N=V=R 가정, ImportanceSampleGGX의 cosθ 표집식 — 두 API 편이 일치. (12) roughness 하한 0.04 — stage2는 specular aliasing, stage3는 GGX 분모 NaN을 이유로 들지만 둘 다 참이라 모순 아님. (13) LUT 해상도(256 vs 512), 프리필터 밉 매핑(roughness×(maxMip−1) vs roughness×MAX_MIP=4) — 구현 선택 차이일 뿐 규약 충돌 아님. (14) stage3 L87 Favg = F0+(1−F0)/21의 계수 21, L89-92 Fdez

**감마 · 색공간** (3건 발견)

■ 모순이 없어 보고하지 않은 대조 항목

1. sRGB 전달함수 상수 — stage0_fund_04 L66~73과 stage1_gl_fund_04 L579~584의 디코딩 조각별 식이 완전히 동일(0.04045, /12.92, ((C+0.055)/1.055)^2.4). 두 편 모두 인코딩 쪽 임계값 0.0031308은 아예 등장하지 않음(누락일 뿐 모순 아님).

2. "감마 2.2 = sRGB" 오동일시 — 다섯 편 어디에도 없음. stage0_fund_04 L58 "대략 γ=2.2의 거듭제곱이지만, 정확히는 ... 조각별 함수", stage1_gl_fund_04 L577 "sRGB는 *대략 감마 2.2*", L586 "근사 ... 정확한 분기 곡선", stage1_gl_proj_05 L565 "정확한 sRGB 곡선은 ... 조각별 함수이지만 단순 2.2 거듭제곱이 시각적으로 충분히 가깝다" — 전부 근사임을 명시. stage0_fund_04 L172는 오히려 "sRGB = 전달함수 vs 색공간" 혼동을 명시적으로 경고.

3. 감마 버그 원인-증상 대응 (지시받은 stage0_fund_04 L98 재확인) — L98을 다시 열어 확인한 결과 **현재 파일에서는 오류가 아니다**. "입력 디코딩을 빼먹고 ... 화면이 씻긴 듯 밝아진다 / 이미 sRGB인 값을 다시 인코딩하는 이중 인코딩도 같은 증상 / 반대로 선형 출력의 sRGB 인코딩을 빼먹으면 ... 전체적으로 어두워진다" — 세 대응 모두 옳다. stage1_gl_fund_04 L879~881(이중 디코드→어두움 / 이중 인코드→밝음·washed out / 출력 인코딩 누락→어두움), stage1_gl_proj_05 L569(이중 감마→밝고 씻긴 듯)도 전부 같은 방향이며 반전된 편은 없다.

4. 톤매핑 위치 — stage0_fund_04 L166("장면 선형 계산을 끝낸 뒤 출력 색역·전달 함수 인코딩 전"), L223~230 다이어그램(scene-linear→노출→톤매핑→색역매핑→출력전달함수), stage1_gl_proj_05 L525~531(HDR FBO→블룸→톤매핑→sRGB 1회, "톤 매핑이 sRGB 변환보다 먼저") 일치. 노출 위치(톤매핑 전 선형 스케일)도 stage0_fund_04 L168과 stage1_gl_proj_05 L478/L558 일치.

5. ACES — stage0_fund_04 L166("ACES Filmic이라는 이름의 짧은 셰이더가 ACES 전체 색 관리 파이프라인과 동일한 것은 아니다")와 stage1_gl_proj_05 L453(IDT→RRT→ODT), L485("ACES AP0/AP1 변환 ... 수행하지 않으므로 'ACES를 구현했다'고 보면 안 된다") 일치. 수치도 직접 검산: Narkowicz 계수 a=2.51,b=0.03,c=2.43,d=0.59,e

**동기화 · 배리어 · 리소스 상태** (2건 발견)

다음을 실제로 열어 대조했고, 모순이 없어 보고하지 않은 항목도 함께 적는다.

[모순 없음 — Vulkan 배리어 stage/access 조합]
- vk_fund_02 L397-415: UNDEFINED→COLOR_ATTACHMENT_OPTIMAL, TOP_OF_PIPE→COLOR_ATTACHMENT_OUTPUT, srcAccess 0 → COLOR_ATTACHMENT_WRITE. 유효.
- vk_proj_01 L375-397: UNDEFINED→TRANSFER_DST(TOP_OF_PIPE→TRANSFER, 0→TRANSFER_WRITE), TRANSFER_DST→SHADER_READ(TRANSFER→FRAGMENT_SHADER, TRANSFER_WRITE→SHADER_READ). 유효하고 fund_02와 일관.
- vk_proj_04 L331-344: DEPTH_ATTACHMENT_OPTIMAL→SHADER_READ_ONLY_OPTIMAL, LATE_FRAGMENT_TESTS→FRAGMENT_SHADER, DEPTH_STENCIL_ATTACHMENT_WRITE→SHADER_READ. access↔stage 짝이 명세상 유효(깊이 쓰기는 EARLY/LATE_FRAGMENT_TESTS에서만 허용, srcStageMask는 지정 스테이지와 그 이전을 포함하므로 LATE로 EARLY까지 커버). subresourceRange {DEPTH,0,1,0,CASCADE_COUNT}도 배열 4층과 일치.
- vk_proj_05 L266-297, L425: COMPUTE_SHADER/SHADER_WRITE → VERTEX_INPUT/VERTEX_ATTRIBUTE_READ. L288-295의 dst 조합표(VERTEX_INPUT↔VERTEX_ATTRIBUTE_READ / INDEX_READ, DRAW_INDIRECT↔INDIRECT_COMMAND_READ, VERTEX/FRAGMENT_SHADER↔SHADER_READ, UNIFORM_READ) 전부 유효하고, L425의 indirect 서술과 표가 일치.
- 동기화2 표기 vk_proj_05 L301의 `VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT` / `VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT` — 실제 Vulkan 1.3 심볼명과 일치.
- vk_proj_06 L556: BLAS write → TLAS read를 ACCELERATION_STRUCTURE_WRITE→ACCELERATION_STRUCTURE_READ로 서술. 유효.

[모순 없음 — 동기화 범위의 방향(과제에서 지목한 항목)]
- 과제문이 확정 오류로 지목한 stage3_vk_proj_01 **L399를 직접 다시 열어 확인**했다. 현재 파일 내용은 「dst stage를 `COLOR_ATTAC

**GPU 아키텍처 · 래스터화 · 깊이** (4건 발견)

■ 깊이 정밀도 수치 — 세 편의 공식·수치를 직접 재유도·재계산해 대조했고, **모순 없음**(과제문에 언급된 두 확정 오류는 이미 수정된 상태로 확인).
· stage0_fund_03 L199 $z_d=\frac{f}{f-n}(1-\frac{n}{z})$ ↔ stage0_fund_06 L96 $z_n=\frac{f(z-n)}{z(f-n)}$ ↔ stage1_gl_fund_05 L248 $z_{ndc}=\frac{f+n}{f-n}+\frac{2fn}{(f-n)z_{view}}$ → 세 식 모두 대수적으로 동치(GL 식은 $[-1,1]$ 규약이며 $(z_{ndc}+1)/2$ 하면 앞 둘과 일치). 경계값 $z{=}n\to0/{-}1$, $z{=}f\to1$ 모두 검산 통과.
· 분해능 $\delta z=\delta z_d\frac{f-n}{fn}z^2$ 재유도 확인. $\delta z_d=2^{-24}$, $n{=}0.1$, $f{=}1000$에서 $z{=}0.1\to5.96\times10^{-9}$m, $z{=}100\to5.96$mm, $z{=}1000\to0.596$m. → stage0_fund_03 L208의 \"$6\times10^{-9}$m / 0.60m\"과 stage1_gl_fund_05 L252의 \"6mm / 0.60m\"이 **같은 n·f에서 같은 값**을 낸다. far 근사 $\delta z\approx\delta z_d f^2/n$(L208)도 재검산 결과 맞고, $n{:}0.1\to0.2$에서 0.60→0.30m, $n{:}0.1\to1.0$에서 약 10배 개선(L252)도 맞음. $(f/n)^2=10^8$ 확인.
· stage0_fund_06 L98~104 표($z$=0.2→0.500, 1.0→0.900, 10→0.990) 재계산 일치.
■ warp/wave 크기 — stage0_adv_01 L34·L290 표와 stage2_dx_proj_05 L128의 NVIDIA 32 / AMD wave32·wave64(GCN wave64) / Intel SIMD8·16·32은 서로 완전 일치. stage0_adv_02 L164만 이탈(보고함).
■ early-Z 무효화 조건 — stage0_fund_06 L74·L172(깊이 직접 쓰기 / `discard`·`clip` / UAV·image 부수 효과)와 stage0_adv_01 L276(불투명 front-to-back, 반투명 back-to-front + 깊이 쓰기 끔)이 상충 없음. stage1_gl_fund_05에는 early-Z 서술이 없어 대조 대상 아님(전체 검색 확인).
■ 공유메모리 뱅크 충돌 — stage0_adv_01 L130~137(32 bank·4바이트 interleave, `tile[32][32]` 열 접근 시 전 lane 뱅크 0, 패딩 33)과 stage0_adv_02 

---

## 반영 상태 (2026-08-13)

이 절은 **본문 반영 작업(2026-08-13)** 을 마친 뒤에 덧붙였다. 위 본문의 지적·근거·정정안은 원문 그대로 두고,
각 지적 제목 옆에 상태 표시만 달았다 — 판정을 나중에 재검토할 근거를 지우지 않기 위해서다.

판정은 추측이 아니라 실제 diff로 했다.

```
diff -ru --strip-trailing-cr "VeLog_backup_2026-08-11/graphics" "VeLog/graphics" \
     -x "REVIEW_*" -x "REDESIGN_*" -x diagrams -x build
```

### 집계

| 분류 | 건수 | 비고 |
|---|---|---|
| **반영됨** | **19 / 19** | 지적 19건 전부 diff에 대응 변경이 있다 |
| **기반영** | **0** | 이 보고서의 지적 중 "작성 후 먼저 고쳐져 있던" 것은 없었다 |
| **기각** | **4항목** | 「대조했으나 모순이 없던 항목」에서 오탐·이미수정으로 확인된 것. **고치면 안 된다** |
| **이월** | **1건 → 0건** | 지적 자체는 반영됐으나 파생 잔여가 남았던 것. **2026-08-14 닫힘** |

지적 19건(NDC 6 · PBR 4 · 감마 3 · 배리어 2 · GPU 4)은 **전부 반영됨**이다.
기각 4항목과 이월 1건은 19건 바깥을 따로 센 수치이므로 19에 더해지지 않는다.

### 반영됨 — 19건

| 주제·번호 | 파일 | 실제로 바뀐 것 |
|---|---|---|
| NDC 1 | stage1_gl_proj_01_cube.md L359 | "DirectX row-major는 `GL_TRUE`" 처방 삭제 → "`GL_FALSE`가 필요한 전치를 대신 만들어 준다. `GL_TRUE`를 주면 결과가 깨진다" |
| NDC 2 | stage1_gl_proj_01_cube.md L675 | "절반의 비트를 음수 표현에 낭비" → "$(z+1)/2$ 재매핑을 거치므로 float의 0 근처 정밀도가 활용되지 못한다" |
| NDC 3 | stage3_vk_fund_01_pipeline.md L188 | "깊이가 절반 범위로 눌려 z-fighting" → "$z_{ndc}$가 음수인 앞쪽 절반이 클립 볼륨 밖으로 잘려 나간다(유효 near ≈ $2n$)" |
| NDC 4 | stage1_gl_proj_01_cube.md L633 | 표 헤더 "NDC z (정규화)" → "깊이 버퍼 값 $z_{\text{win}}=(z_{\text{NDC}}+1)/2$" |
| NDC 5 | stage1_gl_proj_01_cube.md L638·L639 | 0.982 → **0.981**, 0.998 → **0.999** |
| NDC 6 | stage1_gl_proj_01_cube.md L294 | "*M(model) × V(view) × P(projection)*" → "*P(projection) × V(view) × M(model)*" |
| PBR 1 | stage3_vk_proj_03_pbr.md L64·L220 | IBL $k=\alpha^2/2$ → $k=\alpha/2\;(=\text{roughness}^2/2)$ (본문 두 곳. 코드 L236은 원래 옳아 그대로) |
| PBR 2 | stage3_vk_proj_03_pbr.md L276·L278 | `F * brdf.x` → `F0 * brdf.x`, 그리고 설명문에 "다시 각도 의존 $F$를 곱하면 프레넬 이중 적용 / 거칠기 반영 $F$는 $k_D$에만" 추가 |
| PBR 3 | stage2_dx_proj_03_pbr_dx11.md L267~ | 정정안의 **첫 번째 선택지** 채택 — Lagarde $F$와 `kD = (1-F)(1-metallic)`을 IBL 디퓨즈에 도입, L279 서술도 $k_D=(1-F)(1-\text{metallic})$ 표기로 갱신 |
| PBR 4 | stage3_vk_proj_03_pbr.md L136~ | `mipLevels = 1` → `10`, usage에 `TRANSFER_SRC/DST_BIT` 추가, 메모리 "12MB" → "밉 체인 포함 약 16MB", `vkCmdBlitImage` 밉 생성 문장 추가(FIS 전제 명시) |
| 감마 1 | stage1_gl_fund_04_texture.md L599 | "밉맵 다운샘플의 평균" 문구 삭제 → "bilinear 4-탭·trilinear 보간"으로 한정하고, "이 계약은 샘플러 경로에 한한다 / `glGenerateMipmap`은 별개 연산" 문장 추가 |
| 감마 2 | stage1_gl_proj_05_postprocess.md L571 | 무조건적 동치 주장에 조건 부가 — "첨부 color encoding이 실제 `GL_SRGB`일 때만, `GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING` 확인한 경우에 한해" |
| 감마 3 | stage1_gl_proj_05_postprocess.md L392~399 | "그 이상은 모두 1.0이 된다" 삭제 → 오버슈트 서술로 교체, `ReinhardExt` 코드에 `clamp(..., 0.0, 1.0)` 추가 |
| 배리어 1 | stage3_vk_fund_02_vulkan_core.md 6절 | `vkResetFences`를 acquire **뒤로** 이동, acquire 반환값 수신과 `VK_ERROR_OUT_OF_DATE_KHR` 처리 추가 — 9절 L538과 일치 |
| 배리어 2 | stage2_dx_proj_04_dx12_transition.md L520 | "묶으면 한 번의 장벽으로 처리된다" → "드라이버가 이를 합쳐 처리할 여지가 생긴다" (L509·fund_04 L130과 같은 강도) |
| GPU 1 | stage0_fund_03_rasterization.md L208 | "거의 상쇄돼 전 구간 정밀도가 균일해진다" → "상당히 상쇄해 넓은 거리 범위에서 뷰 공간 *상대* 정밀도가 훨씬 고르게 된다(완전 균일 아님, 부동소수점 형식에서만)" |
| GPU 2 | stage1_gl_fund_05_framebuffer.md L241·L256·L258 | "세 가지" → "**네 가지**", `glClipControl`을 필수 항목으로 승격 + far 분해능 $3\times10^{-8}$ 근거와 GL 3.3 Core 단서 추가, L241 표 `GL_DEPTH_COMPONENT32F` 칸에 "(4.5+ `glClipControl` 필요)" 부기 |
| GPU 3 | stage0_adv_02_compute_gpgpu.md L164 앞부분 | "워프(32스레드) … lockstep … 양쪽 경로를 모두 실행" → "실행 묶음(warp/wave/subgroup — 폭은 구현마다 32, 64 등) … 활성 마스크를 달리해 여러 경로를 처리" |
| GPU 4 | stage0_adv_02_compute_gpgpu.md L164 끝문장 | "픽셀 2×2 쿼드도 같은 이유로" → "2×2 쿼드는 원래 화면 도함수(`ddx`/`ddy`, 밉 LOD) 계산을 위한 묶음이고, 분기 일관성은 그 결과" |

### 이월 — 1건 → **2026-08-14 닫힘**

> **[2026-08-14 후속 처리 — 반영됨]**
>
> 아래 이월 항목은 선택지 **(a)** 로 닫혔다. `stage1_gl_fund_04_texture.md` 10절 해설 문단
> (현재 L783, `LoadTexture2D` 코드 블록 직후)에 단서 한 문장을 넣었다 —
> "이 예제는 `sRGB = true` 경로에도 `glGenerateMipmap`을 그대로 쓰는데 이는 흐름을 한 함수로
> 보이기 위한 단순화이고, 배포용 sRGB 디퓨즈의 밉은 4절·8절에서 말한 대로 선형 공간에서
> 디코드→필터→인코드해 오프라인으로 만든 뒤 레벨별로 업로드해야 한다."
>
> (b)(코드에서 `sRGB == true` 경로의 밉 생성 분리)를 택하지 않은 이유: 10절 예제는 "한 함수로
> 전체 흐름을 보이는 것"이 목적이라 분기를 넣으면 그 목적이 흐려지고, 4절 L365·8절 L599가
> 이미 올바른 절차를 규정하고 있어 예제가 그것을 **가리키기만** 하면 부정합이 해소된다.
>
> **이 항목은 다시 열지 마라.** 코드 L772~776 자체는 의도된 단순화이고, 그 사실이 본문에 명시됐다.
>
> 같은 처리 기록이 `graphics/REVIEW_GRAPHICS_2026-08.md` 의 이월 절에도 있다.

<details>
<summary>원래 이월 기록 (2026-08-13)</summary>

**stage1_gl_fund_04_texture.md 10절 통합 예제 — sRGB 텍스처에 `glGenerateMipmap` 직접 호출**

- 현재 위치: 코드 L772~776(`internalFormat = sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;` 직후 `glGenerateMipmap(GL_TEXTURE_2D);`), 설명 L783.
- 감마 1의 정정안은 8절 L599 문장 한정이었고 그대로 반영됐다. 그러나 **예제 코드는 손대지 않았다.**
  이제 4절 L365가 "sRGB 디퓨즈에 `glGenerateMipmap`은 부적절"이라 하고 8절 L599도 "밉 생성은 별개 계약"이라
  못 박은 상태여서, 10절 예제만 그 조합을 아무 단서 없이 쓰고 있다. 감마 1 항목의 근거 문단이 이미
  이 코드(L772~776)를 문제로 지목했으므로, 지적은 해소됐지만 **본문의 실물 부정합은 남아 있다.**
- 남은 일(택1): (a) 예제에 "데모 편의상 자동 생성이며 sRGB 디퓨즈는 오프라인 밉이 옳다"는 한 문장을 단다,
  (b) `sRGB == true` 경로에서 밉 생성을 분리하도록 예제를 고친다.
- 이번 배치에서 하지 않은 이유: 예제 코드 본문 수정이라 정정안 범위 밖이고, 10절 예제 전체의 구성을
  건드리게 된다.

</details>

### 기각 — 4항목 (**고치지 말 것**)

「대조했으나 모순이 없던 항목」 절에 근거가 실려 있다. 과제문·선행 지적이 "확정 오류"라 부른 것을
다시 열어 확인한 결과 **현재 본문이 옳았던** 사례들이다. 2026-08-13 diff에도 해당 파일의 그 자리는 변경이 없다.

1. **stage0_fund_04 L98** — 감마 버그 원인·증상 대응(디코딩 누락→밝음 / 이중 인코딩→밝음 / 출력 인코딩 누락→어두움).
   세 대응 모두 옳다. 「감마 · 색공간」 절의 대조 항목 3 참조.
2. **stage3_vk_proj_03_pbr.md L237 · stage2_dx_proj_03_pbr_dx11.md L240** — `G_SchlickGGX`의 NoV 인자.
   둘 다 이미 `NoV`(uv.x·tid.x 기반)를 쓰고 있어 지목된 `N.z` 결함이 없다. 「PBR 수식」 대조 항목 (9) 참조.
3. **stage3_vk_proj_01_textured_cube.md L399** — 동기화 범위의 방향. 현재 서술("dst stage를 너무 늦게 잡거나
   dstAccess를 비우면 의존성 누락 / 더 이른 stage는 안전하되 보수적")이 옳다. 이 파일은 2026-08-13 diff에
   변경이 전혀 없다.
4. **깊이 정밀도 수치** — stage0_fund_03 L208 · stage1_gl_fund_05 L252 · stage0_fund_06 L98~104의
   공식·수치를 재유도·재계산해 모두 일치 확인. 과제문이 지목한 두 확정 오류는 검수 시점에 이미 수정돼 있었다.

### 기반영 — 0건, 그러나 보고서가 완전히 최신이었던 것은 아니다

math·physics·ai 보고서에서 문제가 된 "보고서 작성 뒤 누군가 먼저 본문에 반영해 놓은 지적"은
**이 보고서의 지적 19건에는 없다** — 19건 모두 2026-08-13 배치에서 처음 반영됐다.

다만 위 기각 2·4번은 "검수 시점에 이미 고쳐져 있던" 사례이고, 보고서가 그것을
「대조했으나 모순이 없던 항목」에 기록해 둔 덕분에 이번 배치에서 헛손질을 하지 않았다.
즉 이 보고서는 **지적 본문은 최신이었고, 낡은 것은 이 보고서가 물려받은 선행 과제문 쪽**이었다.

### 함께 기록해 둘 것

- **이 보고서 지적이 아닌 변경 2건**이 같은 diff에 섞여 있다(다른 보고서 유래로 보임):
  stage0_fund_03_rasterization.md 8절 도입부에 "NDC 깊이 $z$" 가정 추가,
  stage2_dx_fund_04_dx12.md L197 `DrawIndexed` → `DrawIndexedInstanced`. 이 보고서의 집계에는 넣지 않았다.
- **보고서 자체의 알려진 결함(이번에 고치지 않음)**: 「대조했으나 모순이 없던 항목」 절의 407·421·436·446행이
  문장 중간에서 잘려 있다(작성 당시부터 잘린 상태). 원문 훼손을 피하려고 그대로 뒀다.
  잘린 줄의 결론은 각 항목 표제([모순 없음] 등)와 앞부분 서술로 판단했다.
