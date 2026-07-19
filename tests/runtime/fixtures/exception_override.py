def inner():
    try:
        raise ValueError("value")
    finally:
        raise KeyError("key")


try:
    inner()
except KeyError:
    result = 42
