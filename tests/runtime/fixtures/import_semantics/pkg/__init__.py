__all__ = ["CONST", "sub"]
CONST = 1


def import_sibling():
    import sibling
    return sibling


def import_other_value():
    from other import value
    return value
