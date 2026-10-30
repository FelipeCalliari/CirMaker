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
| Menu do botão direito (Device Properties, Edit PROM/RAM, Edit Data Sequencer, Edit Pulser, Edit Terminal Name, recortar/copiar/girar...) | ✅ | |
| Folha que cresce com o circuito (rolar/zoom até qualquer peça) | ✅ | |
| Borda com zonas (Borders) | ✅ | Exibir > Borda; vai junto na impressão e na exportação |
| Auto Wire Routing / Quick Connect / Single Click Connect | 🟡 | arrastar a partir do pino; sem roteamento automático |
| Set Label Positions | ✅ | |
| Group Items (portas no mesmo encapsulamento) | ❌ | falta a pinagem por porta dos CIs |
| Merge (inserir outro circuito) | ✅ | `.cmq` ou `.ckt`, abaixo do circuito atual |
| Copiar esquemático/formas de onda para a área de transferência como imagem | ✅ | Ctrl+Shift+C; formas de onda pelo Editar ou menu da janela |
| Propriedades: Package, Exclude From PCB/Parts, Spice prefix, Pins | 🟡 | designador, rótulo, parâmetros e visibilidade dos textos |
| Cores e fontes configuráveis (Colors, Device/Text Font) | ❌ | cores fixas no estilo do CM |

## Biblioteca

| Recurso | Estado | Observação |
| --- | --- | --- |
| Navegador por categorias e busca | ✅ | |
| Portas, flip-flops, 74xx/40xx de portas, MSI usados no curso | ✅ | 74LS74/73/112, 74139 e 74153 como pacote inteiro; 74LS181 falta |
| Entradas/saídas digitais (chaves, teclados, displays, pulser, SCOPE) | ✅ | |
| Edit PROM/RAM (grade hexadecimal, carregar/salvar .hex/.bin, edição ao vivo durante a simulação) | ✅ | RAM1K e PROM32 |
| Data Sequencer + Pattern Editor | ✅ | clock interno/externo, níveis analógicos (fontes PWL); importado do `.ckt` |
| 7-seg com decodificador, displays ASCII/hex | ✅ | |
| Peças analógicas comuns | ✅ | R, C, L, diodos, BJT, MOSFET, amp-op, fontes, gerador, multímetro |
| Relés, transformadores, válvulas, 555, optoacoplador, SCR/TRIAC | ❌ | importados como `?` |
| Macros (subcircuitos com símbolo próprio, Expand Macro) | 🟡 | New/Edit/Save/Expand/Delete, aninhadas, embutidas no `.cmq`; símbolo gerado (caixa), sem editor de símbolo nem Macro Lock |

## Simulação digital

| Recurso | Estado | Observação |
| --- | --- | --- |
| Run/Pause, Step, Reset, Trace | ✅ | F10, F9, Ctrl+Q, F11 |
| Probe Tool (estado do nó) | ✅ | lâmpada H/L/X/Z no cursor durante a simulação; pontas de prova alimentam as ondas |
| Formas de onda digitais (SCOPE / Active Probe) | ✅ | |
| Atrasos de propagação por dispositivo | ✅ | 1–14 ticks |
| Breakpoints | ✅ | caixinhas dos SCOPEs nas formas de onda (0/1); Nível/Borda × E/Ou |
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
| Abrir/importar `.ckt` (texto, v3–5.61) | ✅ | CIs 74xx/40xx com a ordem e o desenho de pinos do CM; peças giradas/espelhadas; barramentos com nomes numéricos; fios afastados de pinos e de fios de outras redes. Em 167 circuitos de teste restam 7 redes unidas, quase todas por números de rede inconsistentes no próprio arquivo |
| Salvar em `.ckt` | ❌ | salva em `.cmq` |
| Exportar imagem (PNG/SVG/PDF), imprimir | ✅ | |
| Lista de materiais | ✅ | CSV |
| Netlist SPICE / subcircuito SPICE | 🟡 | netlist sim, subcircuito não |
| Netlist PCB | 🟡 | formato Protel; outros formatos do CM não |

## Simulação de falhas (modo professor)

| Recurso | Estado |
| --- | --- |
| Pinos presos em alto/baixo, abertos, curto, valor errado | ✅ |
| Senha de falhas, dicas, trocar dispositivo, desabilitar opções do circuito | ✅ |
