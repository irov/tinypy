#!/usr/bin/env python
from __future__ import print_function

import sys
import unicodedata


FLAG_ALPHA = 1 << 0
FLAG_DIGIT = 1 << 1
FLAG_ALNUM = 1 << 2
FLAG_SPACE = 1 << 3
FLAG_LOWER = 1 << 4
FLAG_UPPER = 1 << 5
FLAG_TITLE = 1 << 6
PAGE_SHIFT = 8
PAGE_COUNT = 0x110000 >> PAGE_SHIFT


def character_flags(character):
    flags = 0
    if character.isalpha():
        flags |= FLAG_ALPHA
    if character.isdigit():
        flags |= FLAG_DIGIT
    if character.isalnum():
        flags |= FLAG_ALNUM
    if character.isspace():
        flags |= FLAG_SPACE
    if character.islower():
        flags |= FLAG_LOWER
    if character.isupper():
        flags |= FLAG_UPPER
    if unicodedata.category(character) == "Lt":
        flags |= FLAG_TITLE
    return flags


def mapped_code_point(character, method):
    mapped = getattr(character, method)()
    if len(mapped) != 1:
        return ord(character)
    return ord(mapped)


def property_page_index(ranges):
    result = []
    index = 0
    for page in xrange(PAGE_COUNT + 1):
        boundary = page << PAGE_SHIFT
        while index < len(ranges) and ranges[index][1] < boundary:
            index += 1
        result.append(index)
    return result


def mapping_page_index(mappings):
    result = []
    index = 0
    for page in xrange(PAGE_COUNT + 1):
        boundary = page << PAGE_SHIFT
        while index < len(mappings) and mappings[index][0] < boundary:
            index += 1
        result.append(index)
    return result


def write_uint16_table(output, name, values):
    print("static const uint16_t %s[] = {" % name, file=output)
    for offset in xrange(0, len(values), 16):
        print("    %s," % ", ".join("UINT16_C(%d)" % value for value in values[offset:offset + 16]), file=output)
    print("};", file=output)


def write_uint8_table(output, name, values):
    print("static const uint8_t %s[] = {" % name, file=output)
    for offset in xrange(0, len(values), 16):
        print("    %s," % ", ".join("UINT8_C(0x%02x)" % value for value in values[offset:offset + 16]), file=output)
    print("};", file=output)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate_unicode_db.py OUTPUT")
    if sys.version_info[:2] != (2, 7) or unicodedata.unidata_version != "5.2.0":
        raise SystemExit("Unicode database generation requires CPython 2.7 with Unicode 5.2.0")

    ranges = []
    mappings = []
    decimal_ranges = []
    numeric_ranges = []
    range_begin = 0
    range_flags = 0
    decimal_begin = None
    decimal_end = None
    numeric_begin = None

    for code_point in xrange(sys.maxunicode + 1):
        character = unichr(code_point)
        flags = character_flags(character)
        if code_point == 0:
            range_flags = flags
        elif flags != range_flags:
            if range_flags != 0:
                ranges.append((range_begin, code_point - 1, range_flags))
            range_begin = code_point
            range_flags = flags

        lower = mapped_code_point(character, "lower")
        upper = mapped_code_point(character, "upper")
        title = mapped_code_point(character, "title")
        if lower != code_point or upper != code_point or title != code_point:
            mappings.append((code_point, lower, upper, title))

        try:
            decimal = unicodedata.decimal(character)
        except ValueError:
            decimal = None
        if decimal is not None and decimal == 0:
            if decimal_begin is not None:
                decimal_ranges.append((decimal_begin, decimal_end))
            decimal_begin = code_point
            decimal_end = code_point
        elif decimal_begin is not None and decimal == code_point - decimal_begin and decimal <= 9:
            decimal_end = code_point
        elif decimal_begin is not None:
            decimal_ranges.append((decimal_begin, decimal_end))
            decimal_begin = None
            decimal_end = None

        try:
            unicodedata.numeric(character)
            numeric = True
        except ValueError:
            numeric = False
        if numeric and numeric_begin is None:
            numeric_begin = code_point
        elif not numeric and numeric_begin is not None:
            numeric_ranges.append((numeric_begin, code_point - 1))
            numeric_begin = None

    if range_flags != 0:
        ranges.append((range_begin, sys.maxunicode, range_flags))
    if decimal_begin is not None:
        decimal_ranges.append((decimal_begin, decimal_end))
    if numeric_begin is not None:
        numeric_ranges.append((numeric_begin, sys.maxunicode))

    property_pages = property_page_index(ranges)
    mapping_pages = mapping_page_index(mappings)
    latin1_flags = [character_flags(unichr(code_point)) for code_point in xrange(256)]
    latin1_mappings = [
        (
            code_point,
            mapped_code_point(unichr(code_point), "lower"),
            mapped_code_point(unichr(code_point), "upper"),
            mapped_code_point(unichr(code_point), "title"),
        )
        for code_point in xrange(256)
    ]

    output = open(sys.argv[1], "w")
    try:
        print("/* Generated by tools/generate_unicode_db.py with CPython %s, Unicode %s. */" % (sys.version.split()[0], __import__("unicodedata").unidata_version), file=output)
        print("#ifndef TINYPY_CORE_GENERATED_UNICODE_DB_H", file=output)
        print("#define TINYPY_CORE_GENERATED_UNICODE_DB_H", file=output)
        print("", file=output)
        write_uint8_table(output, "__tinypy_unicode_latin1_flags", latin1_flags)
        print("", file=output)
        print("static const tinypy_unicode_mapping_t __tinypy_unicode_latin1_mappings[] = {", file=output)
        for code_point, lower, upper, title in latin1_mappings:
            print("    {UINT32_C(0x%06x), UINT32_C(0x%06x), UINT32_C(0x%06x), UINT32_C(0x%06x)}," % (code_point, lower, upper, title), file=output)
        print("};", file=output)
        print("", file=output)
        print("static const tinypy_unicode_property_range_t __tinypy_unicode_property_ranges[] = {", file=output)
        for begin, end, flags in ranges:
            print("    {UINT32_C(0x%06x), UINT32_C(0x%06x), UINT8_C(0x%02x)}," % (begin, end, flags), file=output)
        print("};", file=output)
        print("", file=output)
        write_uint16_table(output, "__tinypy_unicode_property_page_index", property_pages)
        print("", file=output)
        print("static const tinypy_unicode_mapping_t __tinypy_unicode_mappings[] = {", file=output)
        for code_point, lower, upper, title in mappings:
            print("    {UINT32_C(0x%06x), UINT32_C(0x%06x), UINT32_C(0x%06x), UINT32_C(0x%06x)}," % (code_point, lower, upper, title), file=output)
        print("};", file=output)
        print("", file=output)
        write_uint16_table(output, "__tinypy_unicode_mapping_page_index", mapping_pages)
        print("", file=output)
        print("static const tinypy_unicode_decimal_range_t __tinypy_unicode_decimal_ranges[] = {", file=output)
        for begin, end in decimal_ranges:
            print("    {UINT32_C(0x%06x), UINT32_C(0x%06x)}," % (begin, end), file=output)
        print("};", file=output)
        print("", file=output)
        print("static const tinypy_unicode_numeric_range_t __tinypy_unicode_numeric_ranges[] = {", file=output)
        for begin, end in numeric_ranges:
            print("    {UINT32_C(0x%06x), UINT32_C(0x%06x)}," % (begin, end), file=output)
        print("};", file=output)
        print("", file=output)
        print("#endif", file=output)
    finally:
        output.close()


if __name__ == "__main__":
    main()
