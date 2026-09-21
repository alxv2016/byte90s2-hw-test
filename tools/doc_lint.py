#!/usr/bin/env python3
"""Add documentation headers and @brief tags to project code.

Skips vendor/build directories such as .pio, libdeps, .git, and third-party libraries.
"""
from __future__ import annotations

import pathlib
import re
from typing import Iterable

ROOT = pathlib.Path(__file__).resolve().parents[1]

SKIP_DIR_NAMES = {
    '.git',
    '.pio',
    'libdeps',
    'build',
    'dist',
    'node_modules',
    '.vscode',
    '.idea',
}

CODE_ROOTS = {'src', 'lib', 'include'}

FUNC_NAME_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(')
CLASS_RE = re.compile(r'^\s*(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)')
SINGLE_LINE_DOC_RE = re.compile(r'^\s*/\*\*\s*(.*?)\s*\*/\s*$')

STD_HEADERS = {'.h', '.cpp'}


def should_skip(path: pathlib.Path) -> bool:
    return any(part in SKIP_DIR_NAMES for part in path.parts)


def code_files() -> Iterable[pathlib.Path]:
    for path in ROOT.rglob('*'):
        if path.suffix not in STD_HEADERS:
            continue
        if should_skip(path):
            continue
        if not any(part in CODE_ROOTS for part in path.parts):
            continue
        yield path


def has_file_header(lines: list[str]) -> bool:
    i = 0
    while i < len(lines) and (lines[i].strip() == '' or lines[i].strip().startswith('//') or lines[i].strip().startswith('#pragma once')):
        i += 1
    if i < len(lines) and lines[i].strip().startswith('/**'):
        return True
    return False


def file_description(path: pathlib.Path) -> str:
    if path.name == 'main.cpp':
        return 'Application entry point.'
    if path.suffix == '.h':
        return f'Declarations for {path.stem}.'
    return f'Implementation for {path.stem}.'


def add_file_header(path: pathlib.Path, lines: list[str]) -> list[str]:
    if has_file_header(lines):
        return lines
    header = [
        '/**',
        f' * {path.name}',
        ' *',
        f' * {file_description(path)}',
        ' */',
        ''
    ]
    return header + lines


def has_doc_before(lines: list[str], idx: int) -> bool:
    j = idx - 1
    while j >= 0 and lines[j].strip() == '':
        j -= 1
    if j >= 0 and lines[j].strip().endswith('*/'):
        k = j
        while k >= 0 and '/*' not in lines[k]:
            k -= 1
        if k >= 0 and lines[k].strip().startswith('/**'):
            return True
    return False


def add_class_docs(lines: list[str]) -> list[str]:
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = CLASS_RE.match(line)
        if m and not has_doc_before(lines, i):
            name = m.group(2)
            out.extend([
                '/**',
                f' * @brief {name}.',
                ' */'
            ])
        out.append(line)
        i += 1
    return out


def is_function_line(line: str) -> bool:
    s = line.strip()
    if not s or s.startswith('#'):
        return False
    if s.startswith('class ') or s.startswith('struct ') or s.startswith('enum ') or s.startswith('typedef') or s.startswith('using'):
        return False
    if '(' not in s:
        return False
    return s.endswith(';') or s.endswith('{')


def extract_name(lines: list[str], start: int) -> str | None:
    look = []
    k = start
    while k < len(lines) and len(look) < 5:
        if lines[k].strip():
            look.append(lines[k])
            if '(' in lines[k]:
                break
        k += 1
    candidate = ' '.join(look)
    matches = FUNC_NAME_RE.findall(candidate)
    if matches:
        return matches[-1]
    return None


def fix_single_line_docs(lines: list[str]) -> list[str]:
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = SINGLE_LINE_DOC_RE.match(line)
        if m:
            desc = m.group(1).strip()
            j = i + 1
            while j < len(lines) and lines[j].strip() == '':
                j += 1
            name = extract_name(lines, j) if j < len(lines) else None
            brief = name if name else 'Documentation'
            out.extend([
                '/**',
                f' * @brief {brief}.',
                ' *',
                f' * {desc}',
                ' */'
            ])
            i += 1
            # skip any immediately following stray @brief lines
            if j < len(lines) and lines[j].lstrip().startswith('* @brief'):
                j += 1
            continue
        out.append(line)
        i += 1
    return out


def ensure_brief_in_blocks(lines: list[str]) -> list[str]:
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.strip().startswith('/**'):
            block = [line]
            i += 1
            while i < len(lines):
                block.append(lines[i])
                if '*/' in lines[i]:
                    break
                i += 1
            block_text = '
'.join(block)
            j = i + 1
            while j < len(lines) and lines[j].strip() == '':
                j += 1
            if '@brief' not in block_text and j < len(lines) and is_function_line(lines[j]):
                name = extract_name(lines, j)
                if name:
                    block.insert(1, f' * @brief {name}.')
            out.extend(block)
        else:
            out.append(line)
        i += 1
    return out


def process_file(path: pathlib.Path) -> None:
    text = path.read_text(errors='ignore')
    lines = text.splitlines()
    lines = add_file_header(path, lines)
    if path.suffix == '.h':
        lines = add_class_docs(lines)
        lines = fix_single_line_docs(lines)
        lines = ensure_brief_in_blocks(lines)
    path.write_text('
'.join(lines) + ('
' if text.endswith('
') else ''))


def main() -> None:
    for path in code_files():
        process_file(path)


if __name__ == '__main__':
    main()
