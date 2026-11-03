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
| Auto Wire Routing / Quick Connect / Single Click Connect | ✅ | pino → pino/fio contorna os dispositivos sem tocar outros pinos nem correr sobre fios (Opções > Roteamento automático); arrastar a partir do pino |
| Set Label Positions; arrastar designador e rótulo | ✅ | o texto arrasta sozinho (com desfazer); na importação de `.ckt` os textos ficam junto ao símbolo |
| Group Items (portas no mesmo encapsulamento) | ✅ | U1A, U1B...; ao colocar, a porta entra no primeiro encapsulamento com vaga; pinos por parte no netlist de PCB e no BOM; importado do `.ckt` |
| Merge (inserir outro circuito) | ✅ | `.cmq` ou `.ckt`, abaixo do circuito atual |
| Copiar esquemático/formas de onda para a área de transferência como imagem | ✅ | Ctrl+Shift+C; formas de onda pelo Editar ou menu da janela |
| Propriedades: Package, Exclude From PCB/Parts, Spice prefix, Pins | ✅ | designador, rótulo, parâmetros, textos visíveis, encapsulamento, excluir do PCB/BOM, numeração dos pinos, linha SPICE própria (prefixo, modelo, subcircuito) |
| Cores e fontes configuráveis (Colors, Device/Text Font) | ✅ | Preferências: fundo, grade, dispositivos, fios, barramentos, texto, seleção, níveis lógicos; fontes de dispositivo e de texto |

## Biblioteca

| Recurso | Estado | Observação |
| --- | --- | --- |
| Navegador por categorias e busca | ✅ | |
| Portas, flip-flops, 74xx/40xx de portas, MSI usados no curso | ✅ | 74LS74/73/112, 74139 e 74153 como pacote inteiro; 4017, 4093, 74168/169, 74173, 7475, 74181 (ULA) e Quad D Flop |
| Entradas/saídas digitais (chaves, teclados, displays, pulser, SCOPE) | ✅ | cliques rápidos contam todos; chaves e botões clicados recebem o teclado (Espaço/Enter) |
| Edit PROM/RAM (grade hexadecimal, carregar/salvar .hex/.bin, edição ao vivo durante a simulação) | ✅ | RAM1K e PROM32 |
| Data Sequencer + Pattern Editor | ✅ | clock interno/externo, níveis analógicos (fontes PWL); importado do `.ckt` |
| 7-seg com decodificador, displays ASCII/hex | ✅ | |
| Peças analógicas comuns | ✅ | R, C, L, diodos, BJT, MOSFET, amp-op, fontes, gerador, multímetro |
| 555, relé SPDT, transformador (simples e com derivação central), optoacoplador, SCR, TRIAC | ✅ | modelos SPICE; o relé também chaveia na simulação digital |
| Válvulas, motores, alto-falantes, reguladores, JFET, cristal, fusível, ponte retificadora, Darlington, buzzer, solenoide | ✅ | modelos SPICE simplificados (motor/solenoide = R+L, alto-falante/buzzer = R, válvulas pela lei de 3/2) |
| Fontes controladas V→V, V→I, I→V, I→I e chaves controladas por V/I | ✅ | modelos E, G, H, F, S e W do SPICE |
| UJT, PUT, fontes não lineares (NLV/NLI), ADC8/DAC8, linha sem perdas, coaxial, par trançado, blocos gain/divider | ✅ | na simulação analógica; ADC/DAC comportamentais (0/5 V) |
| PLL, VCO, V-Math, blocos diferenciais (gain:A) | ✅ | V-Math1/2 e versões Ref (o rótulo escolhe a função: ABSV, SQRTV, ADDV, MULTV...); gain:A e divider:A com entrada diferencial; VCO senoidal/quadrada/triangular (XSPICE); PLL tipo CD4046 (comparador XOR, VCO de Fc−Fr a Fc+Fr, saída demodulada), com Fc/Fr lidos e gravados no `.ckt` |
| Chave SPDT, relé de diagrama ladder (bobina + contatos NA/NF pelo nome do relé), transformadores por relação de espiras (Trans2, Trans3 com derivação central, xfmr por número de espiras), ADC0800, conector SIP2, placa Caution | ✅ | bobinas (Relay Coil:A, Solenoid) acionam os contatos com o mesmo nome de relé (o terceiro rótulo do CM), com tensão de atracação e de soltura; transformadores ideais (SPICE). Window (janela de alarme: clique abre/fecha, C–NC fechada, C–NO aberta); animados Car (corre ao S, F na chegada, RESET volta; velocidade do `.ckt`), Rocket (decola com + alto) e Stepper (rotor segue as bobinas energizadas, em meio passo, na simulação digital e ao longo do transitório analógico). Ainda sem equivalente: F->V Source e MC1496 |
| Macros (subcircuitos com símbolo próprio, Expand Macro) | ✅ | New/Edit/Save/Expand/Delete, aninhadas, embutidas no `.cmq`, Macro Lock com senha; símbolo em caixa com editor de pinos (lados, ordem, espaços, largura, título) |
| Macros do `User.lib` do CM (Delay, Timer, Lock...) ao abrir `.ckt` | ✅ | lidas do `User.lib` da instalação (não redistribuído): o circuito interno vira uma macro nossa, com portas nas redes de cada pino e os pinos onde o símbolo do CM os tem |

## Simulação digital

| Recurso | Estado | Observação |
| --- | --- | --- |
| Run/Pause, Step, Reset, Trace | ✅ | F10, F9, Ctrl+Q, F11 |
| Probe Tool (estado do nó) | ✅ | lâmpada H/L/X/Z no cursor durante a simulação; clique inverte o nó e Shift+clique o deixa em tristate (um nó forçado por uma saída mantém o nível); pontas de prova alimentam as ondas |
| Formas de onda digitais (SCOPE / Active Probe) | ✅ | |
| Atrasos de propagação por dispositivo | ✅ | 1–14 ticks em portas, flip-flops e CIs MSI; lidos e gravados no `.ckt` (4º número do registro, menos um); Show Prop Delays mostra o atraso no centro de cada dispositivo |
| Saídas em conflito num nó | ✅ | como no CM, vence o dispositivo que vem por último no circuito (verificado no CM via Wine); opção nas Preferências para deixar o nó em X; aviso na barra de status |
| Breakpoints | ✅ | caixinhas dos SCOPEs nas formas de onda (0/1); Nível/Borda × E/Ou |
| Digital Options (tick em tempo real etc.) | ✅ | velocidade em ticks/quadro; tamanho do passo em ticks (10 = um ciclo) ou até o próximo evento |

## Simulação analógica / mista

| Recurso | Estado | Observação |
| --- | --- | --- |
| Ponto de operação, transitório, AC, varredura DC | ✅ | ngspice |
| Modo misto (XSPICE) | ✅ | portas e FFs como modelos XSPICE; CIs MSI rodam o próprio modelo digital dentro do ngspice (d_cosim, `libcmcosim.so`); CIs com pinos bidirecionais (RAM, 74245) ainda não |
| Parameter Sweep, Fourier, Transfer Function, Noise, Temperature, Monte Carlo | ✅ | varreduras repetem qualquer análise e sobrepõem as curvas; Fourier com THD |
| Impedance plot | ✅ | ponta Z na análise AC (\|V/I\| em dBΩ) |
| Janela de análise: cursores, zoom, escala, matemática de ondas, store/recall | ✅ | roda = zoom X, Ctrl+roda = zoom Y, duplo clique enquadra; clique/Shift+clique = cursores A/B com ΔX, 1/ΔX, ΔY; X log; curvas T1-T2, abs, dB; referência tracejada |
| Gerador multifunção: senoide (atraso, amortecimento), pulso, AM, FM, exponencial, PWL | ✅ | tensão ou corrente; fonte AC com magnitude/fase; arquivo .PWL com escala |
| Test points de corrente/potência | ✅ | Probe sobre a peça: I → P → Z → nenhum |
| Importar/simular netlist SPICE | ✅ | R, C, L, D, Q, M, J, V/I (DC, SIN, PULSE) e .op/.tran/.ac/.dc; peças em grade ligadas por terminais com o nome do nó; subcircuitos e fontes controladas ficam de fora |

## Arquivos e exportação

| Recurso | Estado | Observação |
| --- | --- | --- |
| Abrir/importar `.ckt` (texto, v3–5.61) | ✅ | CIs 74xx/40xx com a ordem e o desenho de pinos do CM; peças giradas/espelhadas; barramentos com nomes numéricos; fios afastados de pinos e de fios de outras redes. Em 167 circuitos de teste restam 8 redes unidas, quase todas por números de rede inconsistentes no próprio arquivo ou por fios que o CM desenha a 1 unidade um do outro |
| Salvar em `.ckt` | ✅ | Arquivo > Exportar > CircuitMaker 2000. Requer o CM instalado: os registros vêm do `Device.lib` da instalação (Wine ou pasta escolhida), que não é redistribuído. Testado abrindo os arquivos no CM via Wine: desenho, fios e simulação (chaves, Pulser, portas). Barramentos gravados como no CM (nome e entradas nomeadas ou numeradas); demais rótulos de rede viram Terminals. Ida e volta em 167 circuitos: 165 com as mesmas conexões (os outros dois têm dispositivos sem equivalente no CM ou ligações já refeitas na importação) |
| Exportar imagem (PNG/SVG/PDF), imprimir | ✅ | |
| Lista de materiais | ✅ | CSV |
| Netlist SPICE / subcircuito SPICE | ✅ | subcircuito com as portas Input/Output como pinos |
| Netlist PCB | ✅ | Protel, Tango, PADS, OrCAD PCB II, CadNetix, Calay e Calay 90 (com a lista .cmp), nos layouts de colunas do CM |

## Simulação de falhas (modo professor)

| Recurso | Estado |
| --- | --- |
| Pinos presos em alto/baixo, abertos, curto, valor errado | ✅ |
| Senha de falhas, dicas, trocar dispositivo, desabilitar opções do circuito | ✅ |
