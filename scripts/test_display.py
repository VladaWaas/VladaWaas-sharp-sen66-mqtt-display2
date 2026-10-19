"""Render and check the real dashboard/U8g2 code on the host (no device needed).

Requires installed PlatformIO dependencies (`pio pkg install`) and either
MSVC (Windows), g++, or clang++. Pillow optionally exports PNG previews.
"""
import os
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    output = ROOT / '.pio' / 'display-tests'
    output.mkdir(parents=True, exist_ok=True)
    lib = ROOT / '.pio/libdeps/esp32-c3-devkitm-1/U8g2_for_Adafruit_GFX/src'
    if not lib.is_dir():
        raise SystemExit('Run pio pkg install first.')
    includes = [ROOT / 'test/display', ROOT / 'src', lib]
    sources = [ROOT / 'test/display/render_test.cpp', ROOT / 'src/DisplayDashboard.cpp',
               lib / 'U8g2_for_Adafruit_GFX.cpp']
    # Use the exact bundled numeric font without compiling the entire font catalog
    # on the host. The firmware links this same definition from the library.
    font_name = 'u8g2_font_logisoso34_tn'
    font_source = (lib / 'u8g2_fonts.c').read_text()
    definition = re.search(r'const uint8_t ' + font_name + r'\[.*?;\n', font_source, re.S)
    if not definition:
        raise SystemExit('Numeric font missing from the installed U8g2 library.')
    numeric_font = output / 'numeric_font.cpp'
    numeric_font.write_text('#include <U8g2_for_Adafruit_GFX.h>\n' + definition.group(), encoding='utf-8')
    sources.append(numeric_font)
    cxx = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
    if cxx:
        binary = output / ('render_test.exe' if os.name == 'nt' else 'render_test')
        command = [cxx, '-std=c++17', '-Wall', '-Wextra', '-pedantic',
                   *('-I' + str(p) for p in includes), *(str(p) for p in sources),
                   '-o', str(binary)]
        subprocess.run(command, cwd=output, check=True)
    elif os.name == 'nt':
        vswhere = pathlib.Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*',
            '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            '-property', 'installationPath'], text=True).strip()
        if not installation:
            raise SystemExit('Install a C++ compiler or set CXX.')
        setup = pathlib.Path(installation) / 'VC/Auxiliary/Build/vcvars64.bat'
        binary = output / 'render_test.exe'
        command = ['cl', '/nologo', '/utf-8', '/std:c++17', '/EHsc', '/W3',
                   *('/I' + str(p) for p in includes), *(str(p) for p in sources),
                   '/Fe:' + str(binary)]
        batch = output / 'compile.cmd'
        batch.write_text('@echo off\ncall "' + str(setup) + '" >nul\n' +
                         subprocess.list2cmdline(command) + '\n', encoding='utf-8')
        subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=output, check=True)
    else:
        raise SystemExit('Install g++ or clang++, or set CXX.')
    subprocess.run([str(binary)], cwd=output, check=True)
    try:
        from PIL import Image
    except ImportError:
        return
    for pbm in output.glob('*.pbm'):
        with Image.open(pbm) as image:
            image.save(pbm.with_suffix('.png'))
            image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST).save(
                pbm.with_name(pbm.stem + '-2x.png'))


if __name__ == '__main__':
    main()
