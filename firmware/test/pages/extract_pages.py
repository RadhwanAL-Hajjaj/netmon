"""Pulls the PROGMEM pages out of pages.h the way the compiler would see them."""
import re, sys

def extract(path):
    src = open(path, encoding='utf-8').read()
    # NM_HEAD: a #define of adjacent string literals joined by backslash-newlines.
    m = re.search(r'#define NM_HEAD \\\n(.*?)\n\n', src, re.S)
    head_src = m.group(1)
    head = ''.join(bytes(x, 'utf-8').decode('unicode_escape') for x in re.findall(r'"((?:[^"\\]|\\.)*)"', head_src))
    pages = {}
    for pm in re.finditer(r'static const char (\w+)\[\] PROGMEM =\n"((?:[^"\\]|\\.)*)"\nNM_HEAD\nR"HTML\((.*?)\)HTML";', src, re.S):
        name, first, raw = pm.group(1), pm.group(2), pm.group(3)
        first = bytes(first, 'utf-8').decode('unicode_escape')
        pages[name] = first + head + raw
    return pages

if __name__ == '__main__':
    p = extract(sys.argv[1])
    for k, v in p.items():
        print(k, len(v.encode('utf-8')))
