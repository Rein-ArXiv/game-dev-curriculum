<script setup>
import { computed, ref } from 'vue'

const percent = ref(25)
const speed = ref(2)
const fraction = computed(() => percent.value / 100)
const remaining = computed(() => 1 - fraction.value + fraction.value / speed.value)
const speedup = computed(() => 1 / remaining.value)
const limit = computed(() => percent.value === 100 ? null : 1 / (1 - fraction.value))
const fmt = value => new Intl.NumberFormat('ko-KR', { maximumFractionDigits: 2 }).format(value)
</script>

<template>
  <section class="lab" aria-labelledby="amdahl-heading">
    <p class="lab-label">직접 살펴보기</p>
    <h3 id="amdahl-heading">부분을 빠르게 하면 전체는 얼마나 빨라질까?</h3>
    <p>먼저 예상해 보자. 전체 시간의 25%를 차지하는 부분이 두 배 빨라지면, 전체도 두 배 빨라질까?</p>
    <p class="context">같은 워크로드의 전체 <strong>경과 시간</strong>을 기준으로 한다. 작업량이나 겹쳐 실행된 작업 시간의 합이 아니다.</p>

    <div class="controls">
      <div class="control">
        <label for="amdahl-share">최적화할 부분의 비율 p <strong>{{ percent }}%</strong></label>
        <input id="amdahl-share" v-model.number="percent" type="range" min="0" max="100" step="1" :aria-valuetext="`${percent}%`">
        <div class="endpoints" aria-hidden="true"><span>0%</span><span>100%</span></div>
      </div>
      <div class="control">
        <label for="amdahl-speed">그 부분의 가속 배수 s <strong>{{ speed }}배</strong></label>
        <input id="amdahl-speed" v-model.number="speed" type="range" min="1" max="20" step="0.5" :aria-valuetext="`${speed}배`">
        <div class="endpoints" aria-hidden="true"><span>1배</span><span>20배</span></div>
      </div>
    </div>

    <div class="result" aria-live="polite" aria-atomic="true">
      <p class="result-lead">전체 속도는 <strong>{{ fmt(speedup) }}배</strong>, 경과 시간은 처음의 <strong>{{ fmt(remaining * 100) }}%</strong>다.</p>
      <p class="formula">전체 가속 = 1 ÷ ((1 − {{ fmt(fraction) }}) + {{ fmt(fraction) }} ÷ {{ fmt(speed) }}) = {{ fmt(speedup) }}배</p>
      <div class="bars" aria-label="처음과 최적화 후 경과 시간 비교">
        <div class="bar-row"><span>처음</span><div class="track"><div class="bar before" style="width: 100%"></div></div><strong>100%</strong></div>
        <div class="bar-row"><span>최적화 후</span><div class="track"><div class="bar after" :style="{ width: `${remaining * 100}%` }"></div></div><strong>{{ fmt(remaining * 100) }}%</strong></div>
      </div>
      <p v-if="percent === 0">최적화할 부분이 전체 경과 시간에 없으므로 전체 속도는 바뀌지 않는다.</p>
      <p v-else-if="speed === 1">선택한 부분의 속도가 바뀌지 않았으므로 전체 속도도 바뀌지 않는다.</p>
      <p v-else-if="percent === 100">전체 시간이 최적화 대상이므로 이 모델에서는 전체 가속이 해당 부분의 가속과 같다.</p>
      <p v-else>최적화하지 않은 {{ fmt(100 - percent) }}%는 그대로 남아 전체 개선을 제한한다.</p>
      <p v-if="limit !== null" class="limit">이 부분이 무한히 빨라져도 이 모델의 전체 가속 한계는 {{ fmt(limit) }}배다.</p>
      <p v-else class="limit">전체 시간이 최적화 대상인 이 이상적인 경우에는 유한한 가속 상한이 없다. 실제 시스템에는 다른 비용이 생길 수 있다.</p>
    </div>
    <p class="note">같은 워크로드에서 나머지 구간의 경과 시간은 변하지 않고 추가 비용도 없다고 가정한 계산이며, 실제 측정값은 아니다. 최적화 후에는 다른 부분이 병목이 될 수 있다.</p>
  </section>
</template>

<style scoped>
.lab { margin: 2.5rem 0; padding: 1.5rem; border: 1px solid var(--vp-c-divider); border-radius: 12px; background: var(--vp-c-bg-soft); }
.lab h3 { margin: .25rem 0 .75rem; border: 0; padding: 0; }
.lab p { margin: .75rem 0; line-height: 1.7; }
.lab-label { color: var(--vp-c-brand-1); font-size: .85rem; font-weight: 700; letter-spacing: .02em; }
.context, .note, .limit { color: var(--vp-c-text-2); font-size: .9rem; }
.controls { display: grid; gap: 1.2rem; margin: 1.5rem 0; }
.control label { display: flex; justify-content: space-between; gap: 1rem; font-weight: 600; }
.control input { display: block; width: 100%; margin: .65rem 0 .2rem; accent-color: var(--vp-c-brand-1); }
.control input:focus-visible { outline: 3px solid var(--vp-c-brand-1); outline-offset: 4px; }
.endpoints { display: flex; justify-content: space-between; color: var(--vp-c-text-2); font-size: .8rem; }
.result { padding: 1rem; border-radius: 8px; background: var(--vp-c-bg); }
.result-lead strong { color: var(--vp-c-brand-1); }
.formula { overflow-wrap: anywhere; font-variant-numeric: tabular-nums; font-size: .9rem; }
.bars { display: grid; gap: .7rem; margin: 1.2rem 0; }
.bar-row { display: grid; grid-template-columns: 5rem minmax(0, 1fr) 4.5rem; align-items: center; gap: .55rem; font-size: .85rem; }
.bar-row strong { text-align: right; font-variant-numeric: tabular-nums; }
.track { height: .8rem; overflow: hidden; border-radius: 1rem; background: var(--vp-c-default-soft); }
.bar { height: 100%; border-radius: inherit; }
.before { background: var(--vp-c-text-3); }
.after { background: var(--vp-c-brand-1); }
@media (max-width: 480px) { .lab { padding: 1rem; } .bar-row { grid-template-columns: 4.7rem minmax(0, 1fr) 4rem; gap: .3rem; } }
</style>
