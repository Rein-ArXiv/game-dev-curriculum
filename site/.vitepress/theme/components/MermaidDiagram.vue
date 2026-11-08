<script setup>
import { onMounted, ref, useId } from 'vue'

const props = defineProps({ source: { type: String, required: true } })
const svg = ref('')
const error = ref(false)
const id = `curriculum-diagram-${useId().replace(/[^a-zA-Z0-9_-]/g, '-')}`

onMounted(async () => {
  try {
    const { default: mermaid } = await import('mermaid')
    mermaid.initialize({ startOnLoad: false, securityLevel: 'strict', theme: 'neutral' })
    const result = await mermaid.render(id, props.source)
    svg.value = result.svg
  } catch {
    error.value = true
  }
})
</script>

<template>
  <figure class="diagram">
    <div v-if="svg" class="diagram-image" v-html="svg"></div>
    <div v-else-if="error">
      <p>다이어그램을 표시하지 못했습니다. 원본 내용을 확인할 수 있습니다.</p>
      <pre><code>{{ source }}</code></pre>
    </div>
    <p v-else>다이어그램을 불러오는 중입니다.</p>
  </figure>
</template>

<style scoped>
.diagram { margin: 1.5rem 0; padding: 1rem; overflow-x: auto; border: 1px solid var(--vp-c-divider); border-radius: 8px; background: #fff; color: #263333; }
.diagram-image { min-width: min-content; text-align: center; }
.diagram-image :deep(svg) { max-width: 100%; height: auto; }
.diagram p { margin: 0; }
.diagram pre { text-align: left; white-space: pre-wrap; }
</style>
