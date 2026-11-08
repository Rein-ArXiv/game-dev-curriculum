import DefaultTheme from 'vitepress/theme'
import AmdahlLab from './components/AmdahlLab.vue'
import MermaidDiagram from './components/MermaidDiagram.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('AmdahlLab', AmdahlLab)
    app.component('MermaidDiagram', MermaidDiagram)
  },
}
