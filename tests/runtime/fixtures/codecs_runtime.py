import _codecs


encoded = _codecs.utf_8_encode(u"caf\xe9")
assert encoded == ("caf\xc3\xa9", 4)
assert _codecs.utf_8_decode(encoded[0]) == (u"caf\xe9", 5)
assert _codecs.ascii_encode(u"a\xe9", "replace") == ("a?", 2)
assert _codecs.ascii_decode("a\xff", "replace") == (u"a\ufffd", 2)
assert _codecs.latin_1_encode(u"a\xe9") == ("a\xe9", 2)
assert _codecs.latin_1_decode("a\xe9") == (u"a\xe9", 2)


try:
    _codecs.lookup("missing")
except LookupError:
    pass
else:
    raise AssertionError("lookup must raise LookupError")


assert callable(_codecs.lookup_error("strict"))
