# A continue inside a try, except, finally or with block of a for loop leaves
# the loop iterator on the value stack while the interpreter jumps back to the
# loop target, so the loop block is peeked at instead of being unwound.
log = []


class ContextManager(object):
    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception_value, traceback):
        log.append("exit")
        return False


def for_try_finally():
    del log[:]
    for item in [1, 2, 3]:
        try:
            if item == 2:
                continue
            log.append(item)
        finally:
            log.append("finally")
    return list(log)


assert for_try_finally() == [1, "finally", "finally", 3, "finally"]


def for_try_except():
    del log[:]
    for item in [1, 2, 3]:
        try:
            if item == 2:
                continue
            log.append(item)
        except ValueError:
            log.append("except")
    return list(log)


assert for_try_except() == [1, 3]


def for_with():
    del log[:]
    for item in [1, 2]:
        with ContextManager():
            continue
    return list(log)


assert for_with() == ["exit", "exit"]


def for_nested_try():
    del log[:]
    for item in [1, 2]:
        try:
            try:
                continue
            finally:
                log.append("inner")
        finally:
            log.append("outer")
    return list(log)


assert for_nested_try() == ["inner", "outer", "inner", "outer"]


def for_else_continue():
    del log[:]
    for item in [1, 2]:
        try:
            continue
        finally:
            log.append("finally")
    else:
        log.append("else")
    return list(log)


assert for_else_continue() == ["finally", "finally", "else"]


def nested_for_continue():
    del log[:]
    for outer in [1, 2]:
        for inner in [3, 4]:
            try:
                if inner == 3:
                    continue
                log.append((outer, inner))
            finally:
                log.append("finally")
        log.append(outer)
    return list(log)


assert nested_for_continue() == ["finally", (1, 4), "finally", 1, "finally", (2, 4), "finally", 2]


def while_try_continue():
    del log[:]
    counter = 0
    while counter < 3:
        counter += 1
        try:
            continue
        finally:
            log.append(counter)
    return list(log)


assert while_try_continue() == [1, 2, 3]


def for_try_break():
    for item in [1, 2, 3]:
        try:
            break
        finally:
            log.append("finally")
    return item


assert for_try_break() == 1


def for_try_return():
    for item in [1, 2, 3]:
        try:
            return item
        finally:
            log.append("finally")


assert for_try_return() == 1


def continue_in_comprehension_loop():
    results = []
    for item in [1, 2]:
        try:
            results.append([value for value in [item]])
            continue
        except ValueError:
            pass
    return results


assert continue_in_comprehension_loop() == [[1], [2]]


# break, continue and return inside a finally clause keep the pending unwind
# state of the outer finally, and a loop entered inside the finally unwinds
# independently of it.
def return_then_break_in_finally(values):
    try:
        return values
    finally:
        for value in values:
            break


assert return_then_break_in_finally([1]) == [1]


def return_then_continue_in_finally(values):
    try:
        return 1
    finally:
        for value in values:
            try:
                if value:
                    continue
            except ValueError:
                pass


assert return_then_continue_in_finally([1, 0, 1]) == 1


def break_directly_in_finally():
    del log[:]
    for outer in [1, 2]:
        try:
            log.append(outer)
        finally:
            break
    return list(log)


assert break_directly_in_finally() == [1]


def break_in_nested_finally():
    del log[:]
    for outer in [1, 2]:
        try:
            pass
        finally:
            try:
                break
            finally:
                log.append("inner")
    return list(log)


assert break_in_nested_finally() == ["inner"]


def return_in_nested_finally():
    try:
        pass
    finally:
        try:
            return 1
        finally:
            pass


assert return_in_nested_finally() == 1


def break_from_except_in_finally():
    del log[:]
    for outer in [1, 2]:
        try:
            pass
        finally:
            try:
                raise ValueError
            except ValueError:
                break
    return list(log)


assert break_from_except_in_finally() == []
