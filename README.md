# CirMaker Qt

Editor de esquemáticos e simulador lógico digital em C++20/Qt 6, no espírito do
CircuitMaker 2000 (abandonware). Abre os circuitos `.ckt` do CircuitMaker 2000
e salva no formato próprio `.cmq` (JSON). Interface em português e inglês.

## Compilar e testar

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/cirmaker [circuito.cmq | circuito.ckt]
```

Requer Qt ≥ 6.5 (Core, Gui, Widgets, Svg, PrintSupport, LinguistTools, Test).
`-DCM_BUILD_APP=OFF` compila só núcleo, simulador e testes.

Linha de comando:

- `cirmaker arquivo.ckt --export saida.png` renderiza (PNG/SVG/PDF) e sai;
- `cirmaker --library-sheet folha.png` desenha todos os símbolos da biblioteca.

O teste de importação lê os `.ckt` dos diretórios em `CM_CKT_DIRS`
(separados por `:`); sem a variável, procura os exemplos do CircuitMaker no
Wine e `~/Documents/github/_calliari_/tecdigi/CircuitMaker`.

## Uso

- **Dispositivos** (painel à esquerda): busque pelo nome ou pelo nome do
  CircuitMaker (`4027`, `2-In AND`…), dê duplo clique e clique no esquemático.
  `R` gira e `M` espelha durante a colocação; Shift continua colocando.
- **Fios**: ferramenta Fio (`W`) ou arraste a partir da ponta de um pino.
  Terminais com o mesmo rótulo ficam na mesma rede.
- **Barramento** (`B`): desenhe a linha grossa; um fio que termina nela pede
  o nome da entrada, e entradas com o mesmo nome ficam ligadas (a linha em si
  não conduz). **Rótulo de rede** (`N`) liga pontos pelo nome em qualquer lugar.
- **Localizar e selecionar** (`Ctrl+F`): por designador, rótulo ou dispositivo.
- **Bloco de título**: Editar → Bloco de título; arraste para posicionar,
  Exibir → Mostrar bloco de título liga e desliga. Sai na exportação e impressão.
- **Lista de materiais**: Arquivo → Lista de materiais (CSV), agrupada por
  dispositivo e valor.
- **Simulação**: `F9` executa/pausa, `F10` avança até a próxima mudança,
  `Ctrl+F9` reinicia, `Shift+F9` volta à edição. Durante a simulação clique em
  Logic Switch, push buttons e Hex Key. Fios vermelhos = 1, azuis = 0,
  laranja = conflito (X), cinza = alta impedância.

## Simulação analógica (SPICE)

Requer o ngspice compilado como biblioteca compartilhada (`libngspice.so`,
pacote `ngspice` no Arch); ele é carregado em tempo de execução, então o
programa funciona sem ele no modo digital.

- Simulação → **Modo analógico** (ou a caixa Digital/Analógico na barra).
- Simulação → **Análise analógica**: ponto de operação, transitório,
  varredura AC (geradores de sinal são as fontes AC de 1 V) ou varredura DC.
- **Ponta de prova** (`P`): clique nos fios cujas tensões quer traçar; sem
  pontas, são traçadas as redes com nome.
- `F9` roda o ngspice em segundo plano. O ponto de operação aparece nos fios;
  transitório, AC (em dB, eixo log) e DC aparecem no painel **Formas de onda**,
  com cursor de leitura e exportação CSV/PNG (botão direito). Multímetros,
  lâmpadas, LEDs e displays mostram o valor final.
- Peças: resistor, potenciômetro, capacitor, eletrolítico, indutor, diodo
  (1N4148/1N4001), zener 5,1 V, LED, BJT 2N3904/2N3906, MOSFET N/P, amp-op
  comportamental (100 dB, 1 MHz), fontes DC, bateria, fonte de corrente,
  gerador (senoide, quadrada, triangular), lâmpada, multímetro (V/A), `.IC` e
  `.NODESET`.
- **Modo misto**: portas, buffers, tri-states e flip-flops viram modelos XSPICE
  com pontes A/D (limiares 0,8/2,0 V, saídas 0/5 V); chaves lógicas, pulser e
  hex key viram fontes. Um tick digital vale 1 µs. CIs MSI ainda não têm
  modelo analógico e ficam de fora com aviso.

## Estrutura

| Pasta | Conteúdo |
| --- | --- |
| `src/core` | modelo do circuito, conectividade (`NetlistBuilder`), undo, formatos `.cmq` e `.ckt` |
| `src/library` | carregamento dos símbolos JSON e geradores de portas e caixas |
| `src/sim/digital` | simulador por eventos (lógica 0/1/X/Z, força forte/fraca), modelos de portas, flip-flops e CIs MSI |
| `src/sim/analog` | gerador de netlist SPICE e ponte com o ngspice (carregado via `QLibrary`) |
| `src/ui` | janela principal, cena/vista do esquemático, navegador de dispositivos |
| `library/symbols` | biblioteca gerada por `tools/gen_library.py` (não editar à mão) |
| `i18n` | tradução pt-BR (`cmake --build build --target update_translations` atualiza) |

## Importação do CircuitMaker 2000

O formato `CircuitMaker Text` (versões 3.x a 5.61) foi recuperado por engenharia
reversa do `cirmaker.exe`; a gramática está documentada no topo de
`src/core/io/CktImporter.cpp`. A geometria é escalada por 10/3 (grade de 3 do
CM para a grade de 10 daqui). As pontas de fio presas a pinos são levadas aos
pinos dos símbolos novos, e as redes gravadas no arquivo são conferidas: onde o
desenho sozinho não reproduz a ligação original (barramentos, por exemplo),
o importador acrescenta rótulos de rede pequenos.

Nenhum arquivo do CircuitMaker é distribuído: os símbolos são desenhos
próprios, e apenas as posições dos pinos foram medidas nos circuitos `.ckt`.
Peças sem equivalente (74LS181, relés, transformadores, válvulas, 555,
instrumentos como o osciloscópio do CM…) entram como marcadores `?`.
Circuitos com peças só analógicas abrem em modo analógico.

A PROM32 importada traz o conteúdo gravado no `.ckt` na propriedade `data`
(bytes em hexadecimal, editáveis em Propriedades); a 1K RAM começa zerada.
