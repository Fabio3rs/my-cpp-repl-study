#!/bin/bash
# Script to download Lucide icons for CPPREPL project
# License: MIT (Lucide icons are MIT licensed)

ICONS_DIR="assets/icons"
BASE_URL="https://raw.githubusercontent.com/lucide-icons/lucide/main/icons"

# Criar diretório
mkdir -p "$ICONS_DIR"

# Lista de ícones necessários para CPPREPL
ICONS=(
    "circle-alert"      # erro crítico (notificações)
    "triangle-alert"    # warning/aviso
    "circle-x"         # falha/erro
    "info"             # informação geral
    "circle-check"     # sucesso/compilado
    "check"            # confirmação simples
    "terminal"         # ícone principal do REPL
    "code"             # código/programação
    "file-code"        # arquivo de código
    "settings"         # build/configurações
    "folder"           # diretórios/includes
    "book-open"        # documentação
    "lightbulb"        # dicas/sugestões
    "search"           # busca/completion
    "loader"           # loading/processando
    "circle-play"      # executar código
    "help"             # ajuda/dúvidas (nome simples)
)

# Definir ícones críticos que devem existir obrigatoriamente
CRITICAL_ICONS=("circle-alert" "info" "terminal" "code" "check")

# Verificar se os ícones críticos já existem
echo "🔍 Checking if icons already exist..."
all_critical_exist=true
existing_count=0

for icon in "${ICONS[@]}"; do
    if [ -f "$ICONS_DIR/$icon.svg" ]; then
        ((existing_count++))
    fi
done

# Se todos os ícones críticos existem, não precisa fazer download
missing_critical=0
for critical in "${CRITICAL_ICONS[@]}"; do
    if [ ! -f "$ICONS_DIR/$critical.svg" ]; then
        ((missing_critical++))
        all_critical_exist=false
    fi
done

if [ $all_critical_exist = true ]; then
    echo "✅ All critical icons already exist ($existing_count/${#ICONS[@]} total icons found)"
    echo "   📁 Location: $ICONS_DIR/"
    echo "   🚀 Skipping download - build can proceed safely."

    # Create README if it doesn't exist
    if [ ! -f "$ICONS_DIR/README.md" ]; then
        cat > "$ICONS_DIR/README.md" << 'EOF'
# CPPREPL Icons

This directory contains icons from [Lucide Icons](https://lucide.dev/) under the MIT License.

## Usage

- `circle-alert.svg` - Critical errors (notifications)
- `triangle-alert.svg` - Warnings
- `circle-x.svg` - Failures/errors
- `info.svg` - General information
- `circle-check.svg` - Success/compiled
- `terminal.svg` - Main REPL icon
- `code.svg` - Code/programming
- `settings.svg` - Build/configuration
- `search.svg` - Search/completion

## License

All icons are from Lucide Icons (https://lucide.dev/) and are licensed under the MIT License.
Permission is hereby granted, free of charge, to any person obtaining a copy of these icons.

## Attribution

Icons by Lucide Contributors - https://github.com/lucide-icons/lucide
EOF
        echo "   📝 Created: $ICONS_DIR/README.md"
    fi

    exit 0
fi

echo "⚠️  Missing $missing_critical critical icons, proceeding with download..."
echo ""

echo "🎨 Downloading Lucide icons for CPPREPL..."
echo "   Source: https://lucide.dev/ (MIT License)"
echo "   Target: $ICONS_DIR/"
echo ""

success_count=0
error_count=0

for icon in "${ICONS[@]}"; do
    echo -n "  → $icon.svg ... "

    if wget -q "$BASE_URL/$icon.svg" -O "$ICONS_DIR/$icon.svg" 2>/dev/null; then
        echo "✅"
        ((success_count++))
    else
        echo "❌ (download failed)"
        ((error_count++))
        # Remove arquivo vazio se houver
        rm -f "$ICONS_DIR/$icon.svg"
    fi
done

echo ""
echo "📊 Summary:"
echo "   ✅ Downloaded: $success_count icons"
echo "   ❌ Failed: $error_count icons"
echo "   📁 Location: $ICONS_DIR/"

if [ $success_count -gt 0 ]; then
    echo ""
    echo "🚀 Icons downloaded successfully!"
    echo "   Downloaded $success_count out of ${#ICONS[@]} icons."
    if [ $error_count -gt 0 ]; then
        echo "   ⚠️  Some icons are not available, but build can continue."
    fi
else
    echo ""
    echo "❌ No icons could be downloaded."
    echo "   Check your internet connection and try again."
    exit 1
fi

# Create a README for the icons directory
cat > "$ICONS_DIR/README.md" << 'EOF'
# CPPREPL Icons

This directory contains icons from [Lucide Icons](https://lucide.dev/) under the MIT License.

## Usage

- `circle-alert.svg` - Critical errors (notifications)
- `triangle-alert.svg` - Warnings
- `circle-x.svg` - Failures/errors
- `info.svg` - General information
- `circle-check.svg` - Success/compiled
- `terminal.svg` - Main REPL icon
- `code.svg` - Code/programming
- `settings.svg` - Build/configuration
- `search.svg` - Search/completion

## License

All icons are from Lucide Icons (https://lucide.dev/) and are licensed under the MIT License.
Permission is hereby granted, free of charge, to any person obtaining a copy of these icons.

## Attribution

Icons by Lucide Contributors - https://github.com/lucide-icons/lucide
EOF

echo "   📝 Created: $ICONS_DIR/README.md"
