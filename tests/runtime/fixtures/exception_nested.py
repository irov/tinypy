def inner():
    raise KeyError("key")


def outer():
    try:
        inner()
    except LookupError:
        return 42


result = outer()
