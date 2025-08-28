#!/bin/bash
# Script to convert SVG icons to PNG format

ICONS_DIR="${1:-assets/icons}"
PNG_DIR="${2:-assets/icons/png}"
SIZE="${3:-32}"

# Create PNG directory
mkdir -p "$PNG_DIR"

# Check if conversion is needed
echo "🔍 Checking if PNG conversion is needed..."
svg_count=0
png_count=0
conversion_needed=false

# Count existing SVG and PNG files
for svg in "$ICONS_DIR"/*.svg; do
    if [ -f "$svg" ]; then
        ((svg_count++))
        basename_svg=$(basename "$svg" .svg)
        png="$PNG_DIR/${basename_svg}.png"

        if [ -f "$png" ]; then
            # Check if PNG is newer than SVG
            if [ "$svg" -nt "$png" ]; then
                conversion_needed=true
            else
                ((png_count++))
            fi
        else
            conversion_needed=true
        fi
    fi
done

if [ $svg_count -eq 0 ]; then
    echo "❌ No SVG files found in $ICONS_DIR/"
    echo "   Run download_icons.sh first to get the source icons."
    exit 1
fi

if [ $conversion_needed = false ] && [ $png_count -eq $svg_count ]; then
    echo "✅ All PNG files are up-to-date ($png_count/$svg_count files)"
    echo "   📁 Location: $PNG_DIR/"
    echo "   🚀 Skipping conversion - build can proceed safely."
    exit 0
fi

echo "⚠️  Conversion needed: $png_count/$svg_count PNG files are up-to-date"
echo ""

echo "🔄 Converting SVG icons to PNG format..."
echo "   Source: $ICONS_DIR/"
echo "   Target: $PNG_DIR/"
echo "   Size: ${SIZE}x${SIZE}px"
echo ""

if ! command -v inkscape >/dev/null 2>&1; then
    echo "❌ Inkscape not found. Cannot convert SVG to PNG."
    echo "   Install Inkscape with: sudo apt install inkscape"
    exit 1
fi

success_count=0
error_count=0

for svg in "$ICONS_DIR"/*.svg; do
    if [ -f "$svg" ]; then
        basename_svg=$(basename "$svg" .svg)
        png="$PNG_DIR/${basename_svg}.png"

        # Only convert if PNG doesn't exist or SVG is newer
        if [ ! -f "$png" ] || [ "$svg" -nt "$png" ]; then
            echo -n "  → ${basename_svg}.png ... "

            if inkscape --export-type=png --export-width=$SIZE --export-height=$SIZE "$svg" --export-filename="$png" >/dev/null 2>&1; then
                echo "✅"
                ((success_count++))
            else
                echo "❌ (conversion failed)"
                ((error_count++))
                # Remove arquivo vazio se houver
                rm -f "$png"
            fi
        else
            echo "  → ${basename_svg}.png ... ⏭️  (already up-to-date)"
            ((success_count++))
        fi
    fi
done

echo ""
echo "📊 Summary:"
echo "   ✅ Converted: $success_count icons"
echo "   ❌ Failed: $error_count icons"
echo "   📁 Location: $PNG_DIR/"

if [ $success_count -gt 0 ]; then
    echo ""
    echo "🚀 PNG conversion completed successfully!"

    # Verificar se os ícones críticos foram convertidos
    critical_icons=("circle-alert" "info" "terminal" "code" "check")
    missing_critical=0

    echo ""
    echo "🔍 Checking critical PNG conversions..."
    for critical in "${critical_icons[@]}"; do
        if [ -f "$PNG_DIR/$critical.png" ]; then
            echo "   ✅ $critical.png - OK"
        else
            echo "   ⚠️  $critical.png - MISSING (will use SVG fallback)"
            # Não conta como erro crítico pois SVG pode ser usado
        fi
    done

    # Verificar se pelo menos alguns PNGs foram criados
    png_count=$(ls -1 "$PNG_DIR"/*.png 2>/dev/null | wc -l)
    if [ $png_count -eq 0 ]; then
        echo ""
        echo "❌ No PNG files were created successfully."
        echo "   Application will fall back to SVG icons."
        exit 1
    fi

    echo ""
    echo "✅ PNG conversion completed. Created $png_count PNG files."
else
    echo ""
    echo "❌ No icons could be converted."
    echo "   Application will use SVG icons only."
    exit 1
fi
