import _codecs


encoded = _codecs.utf_8_encode(u"caf\xe9")
assert encoded == ("caf\xc3\xa9", 4)
assert _codecs.utf_8_decode(encoded[0]) == (u"caf\xe9", 5)
assert _codecs.ascii_encode(u"a\xe9", "replace") == ("a?", 2)
assert _codecs.ascii_decode("a\xff", "replace") == (u"a\ufffd", 2)
assert _codecs.latin_1_encode(u"a\xe9") == ("a\xe9", 2)
assert _codecs.latin_1_decode("a\xe9") == (u"a\xe9", 2)
assert "\xc0\xaf".decode("utf-8", "replace") == u"\ufffd\ufffd"
assert "\xe2\x82".decode("utf-8", "replace") == u"\ufffd"
assert "\xe2(\xa1".decode("utf-8", "replace") == u"\ufffd(\ufffd"
assert "\xf4\x90\x80\x80".decode("utf-8", "replace") == u"\ufffd\ufffd"
assert "\xf0(\x8c\xbc".decode("utf-8", "replace") == u"\ufffd(\ufffd\ufffd"
assert "ascii".encode("utf-8") == "ascii"
assert u"ascii".decode("utf-8") == u"ascii"
assert u"\ud834".encode("utf-8") == "\xed\xa0\xb4"
assert u"\ud834\udd20".encode("utf-8") == "\xf0\x9d\x84\xa0"
assert u"\U0001d120".encode("utf-8") == "\xf0\x9d\x84\xa0"

for codec_errors in ("strict", "ignore", "replace"):
    try:
        "caf\xe9".encode("utf-8", codec_errors)
    except UnicodeDecodeError:
        pass
    else:
        raise AssertionError("byte-string encode skipped the default ASCII decode")

    try:
        u"caf\xe9".decode("utf-8", codec_errors)
    except UnicodeEncodeError:
        pass
    else:
        raise AssertionError("Unicode decode skipped the default ASCII encode")


try:
    _codecs.lookup("missing")
except LookupError:
    pass
else:
    raise AssertionError("lookup must raise LookupError")


assert callable(_codecs.lookup_error("strict"))

# Error handlers are looked up lazily, matching CPython: a missing handler is
# harmless until the codec actually encounters invalid data.
assert "ascii".decode("ascii", "missing-handler") == u"ascii"
try:
    "\xff".decode("ascii", "missing-handler")
except LookupError:
    pass
else:
    raise AssertionError("missing decode handler did not raise LookupError")

try:
    "\xff".decode("ascii")
except UnicodeDecodeError:
    pass
else:
    raise AssertionError("strict ASCII decode did not raise UnicodeDecodeError")

try:
    u"\u20ac".encode("ascii")
except UnicodeEncodeError:
    pass
else:
    raise AssertionError("strict ASCII encode did not raise UnicodeEncodeError")

try:
    "text".decode("missing-codec")
except LookupError:
    pass
else:
    raise AssertionError("unknown codec did not raise LookupError")
