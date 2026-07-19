def inner():
    try:
        raise IndexError("index")
    except IndexError:
        raise


try:
    inner()
except LookupError:
    result = 42
