try:
    raise ValueError("bad")
except ValueError:
    result = 42
