# Paridade com o CircuitMaker 2000

Checklist montado a partir do help do CircuitMaker 2000 (`Cirmaker.hlp`,
873 tópicos). ✅ feito · 🟡 parcial · ❌ falta.

## Desenho e edição

| Recurso | Estado | Observação |
| --- | --- | --- |
| Ferramentas seta, fio, texto, apagar, zoom, ponta de prova | ✅ | Alt+A/W/T/D/Z/P |
| Girar, espelhar, recortar/copiar/colar, duplicar, desfazer | ✅ | |
| Mover componentes levando os fios | ✅ | |
| Arrastar segmentos e pontas de fio; cortar segmento (Shift+apagar) | ✅ | |
| Barramentos com nome e entradas nomeadas | ✅ | |
| Rótulos de nó (Place Node Label) | ✅ | Alt+N |
| Destacar nó inteiro (Alt+clique) | ✅ | |
| Nudge com as setas, Page Up/Down, Home, End | ✅ | |
| Localizar e selecionar | ✅ | |
| Set Designations, Straighten Wires, Set Prop Delays | ✅ | |
| HotKeys de dispositivo (0–9, a–z, A–Z) | ✅ | |
| Mostrar nomes dos nós / pontos nos pinos | ✅ | |
| Grade, bloco de título | ✅ | |
| Borda com zonas (Borders) | ❌ | |
| Auto Wire Routing / Quick Connect / Single Click Connect | 🟡 | arrastar a partir do pino; sem roteamento automático |
| Set Label Positions, Group Items | ❌ | |
| Merge (inserir outro circuito) | ❌ | |
| Copiar esquemático/formas de onda para a área de transferência como imagem | ❌ | |
| Propriedades: Package, Exclude From PCB/Parts, Spice prefix, Pins | 🟡 | designador, rótulo, parâmetros e visibilidade dos textos |
| Cores e fontes configuráveis (Colors, Device/Text Font) | ❌ | cores fixas no estilo do CM |

## Biblioteca

| Recurso | Estado | Observação |
| --- | --- | --- |
| Navegador por categorias e busca | ✅ | |
| Portas, flip-flops, 74xx/40xx de portas, MSI usados no curso | ✅ | 74LS181 falta |
| Entradas/saídas digitais (chaves, teclados, displays, pulser, SCOPE) | ✅ | |
| Data Sequencer + Pattern Editor | ❌ | |
| 7-seg com decodificador, displays ASCII/hex | ✅ | |
| Peças analógicas comuns | ✅ | R, C, L, diodos, BJT, MOSFET, amp-op, fontes, gerador, multímetro |
| Relés, transformadores, válvulas, 555, optoacoplador, SCR/TRIAC | ❌ | importados como `?` |
| Macros (subcircuitos com símbolo próprio, Expand Macro) | ❌ | |

## Simulação digital

| Recurso | Estado | Observação |
| --- | --- | --- |
| Run/Pause, Step, Reset, Trace | ✅ | F10, F9, Ctrl+Q, F11 |
| Probe Tool (estado do nó) | 🟡 | pontas de prova alimentam as ondas; sem lâmpada de leitura no cursor |
| Formas de onda digitais (SCOPE / Active Probe) | ✅ | |
| Atrasos de propagação por dispositivo | ✅ | 1–14 ticks |
| Breakpoints | ❌ | |
| Digital Options (tick em tempo real etc.) | 🟡 | velocidade em ticks/quadro |

## Simulação analógica / mista

| Recurso | Estado | Observação |
| --- | --- | --- |
| Ponto de operação, transitório, AC, varredura DC | ✅ | ngspice |
| Modo misto (XSPICE) | 🟡 | portas e FFs; CIs MSI não |
| Parameter Sweep, Fourier, Transfer Function, Noise, Temperature, Monte Carlo, Impedance | ❌ | |
| Janela de análise: cursores, zoom, escala, matemática de ondas, store/recall | 🟡 | cursor de leitura e exportação CSV/PNG |
| Gerador multifunção: AM, FM, exponencial, PWL | 🟡 | senoide, quadrada, triangular |
| Test points de corrente/potência | ❌ | só tensões e multímetro |
| Importar/simular netlist SPICE | ❌ | exporta sim |

## Arquivos e exportação

| Recurso | Estado | Observação |
| --- | --- | --- |
| Abrir/importar `.ckt` (texto, v3–5.61) | ✅ | |
| Salvar em `.ckt` | ❌ | salva em `.cmq` |
| Exportar imagem (PNG/SVG/PDF), imprimir | ✅ | |
| Lista de materiais | ✅ | CSV |
| Netlist SPICE / subcircuito SPICE | 🟡 | netlist sim, subcircuito não |
| Netlist PCB | 🟡 | formato Protel; outros formatos do CM não |

## Simulação de falhas (modo professor)

| Recurso | Estado |
| --- | --- |
| Pinos presos em alto/baixo, abertos, curto, valor errado | ❌ |
| Senha de falhas, dicas, desabilitar opções do circuito | ❌ |
