import sibling

sibling_value = sibling.value

# A partially matching implicit relative import must not fall back to an
# absolute import of the remaining name.
try:
    import sibling.zzz
except ImportError as error:
    partial_error = str(error)
else:
    partial_error = None

try:
    from ... import nothing
except ValueError as error:
    beyond_error = str(error)
else:
    beyond_error = None

from .. import plain

plain_value = plain.value
