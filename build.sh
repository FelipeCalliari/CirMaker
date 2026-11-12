#!/usr/bin/env bash
# Compila o CirMaker no Linux.
#
#   ./build.sh                 Release em build/
#   ./build.sh --debug         Debug
#   ./build.sh --test          compila e roda os testes (ctest)
#   ./build.sh --package       compila e gera dist/CirMaker-<versão>-linux-x86_64.tar.gz
#   ./build.sh --clean         apaga o diretório de build antes
#
# Variáveis: BUILD_DIR (padrão build), QT_DIR (prefixo do Qt, ex.
# ~/Qt/6.8.3/gcc_64, se não for o do sistema), JOBS (padrão: nº de CPUs).
# Requer CMake ≥ 3.21, um compilador C++20 e Qt ≥ 6.5; Ninja é usado se houver.
# A simulação analógica usa a libngspice instalada no sistema (pacote ngspice).
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE=Release
RUN_TESTS=0
PACKAGE=0
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE=Debug ;;
        --test)    RUN_TESTS=1 ;;
        --package) PACKAGE=1 ;;
        --clean)   CLEAN=1 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $arg (veja --help)" >&2; exit 2 ;;
    esac
done

BUILD_DIR=${BUILD_DIR:-build}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

for tool in cmake c++; do
    command -v "$tool" >/dev/null || { echo "erro: '$tool' não encontrado" >&2; exit 1; }
done

[[ $CLEAN == 1 ]] && rm -rf "$BUILD_DIR"

args=(-S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
# um diretório já configurado mantém o gerador com que foi criado
if [[ ! -f $BUILD_DIR/CMakeCache.txt ]] && command -v ninja >/dev/null; then
    args+=(-G Ninja)
fi
[[ -n ${QT_DIR:-} ]] && args+=(-DCMAKE_PREFIX_PATH="$QT_DIR")

echo "==> Configurando ($BUILD_TYPE) em $BUILD_DIR"
cmake "${args[@]}"
echo "==> Compilando"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [[ $RUN_TESTS == 1 ]]; then
    echo "==> Testes"
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [[ $PACKAGE == 1 ]]; then
    version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")
    name="CirMaker-${version}-linux-$(uname -m)"
    stage="dist/$name"
    echo "==> Empacotando $stage.tar.gz"
    rm -rf "$stage"
    mkdir -p "$stage"
    cp "$BUILD_DIR/cirmaker" "$BUILD_DIR/libcmcosim.so" "$stage/"
    cp README.md "$stage/"
    [[ -f LICENSE ]] && cp LICENSE "$stage/"
    tar -C dist -czf "dist/$name.tar.gz" "$name"
    echo "    dist/$name.tar.gz (usa o Qt e o ngspice do sistema)"
fi

echo "==> Pronto: $BUILD_DIR/cirmaker"
