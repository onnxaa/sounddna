#!/usr/bin/env python3
"""Generate a C++ source file embedding the web UI as a raw string literal."""

import re
import sys
import os

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <web_dir> <output_cpp>", file=sys.stderr)
        sys.exit(1)

    web_dir = sys.argv[1]
    output_cpp = sys.argv[2]

    html_path = os.path.join(web_dir, "index.html")
    if not os.path.isfile(html_path):
        print(f"Error: {html_path} not found", file=sys.stderr)
        sys.exit(1)

    with open(html_path, "r", encoding="utf-8") as f:
        html = f.read()

    def inline_css(match):
        href = match.group(1)
        css_path = os.path.join(web_dir, href)
        if os.path.isfile(css_path):
            with open(css_path, "r", encoding="utf-8") as f:
                css = f.read()
            return f"<style>\n{css}\n</style>"
        return match.group(0)

    def inline_js(match):
        src = match.group(1)
        js_path = os.path.join(web_dir, src)
        if os.path.isfile(js_path):
            with open(js_path, "r", encoding="utf-8") as f:
                js = f.read()
            return f"<script>\n{js}\n</script>"
        return match.group(0)

    html = re.sub(
        r'<link\s+rel="stylesheet"\s+href="([^"]+)"\s*/?>',
        inline_css,
        html,
    )

    html = re.sub(
        r'<script\s+src="([^"]+)"\s*></script>',
        inline_js,
        html,
    )

    os.makedirs(os.path.dirname(output_cpp), exist_ok=True)
    with open(output_cpp, "w", encoding="utf-8") as f:
        f.write('// Auto-generated UI resource file. Do not edit.\n')
        f.write('#include "IPlugPlatform.h"\n\n')
        f.write('BEGIN_IPLUG_NAMESPACE\n\n')
        f.write('const char* kSoundDNAUIHTML = R"NRUI_END(\n')
        f.write(html)
        f.write('\n)NRUI_END";\n\n')
        f.write('END_IPLUG_NAMESPACE\n')

    print(f"Generated {output_cpp}")

if __name__ == "__main__":
    main()
