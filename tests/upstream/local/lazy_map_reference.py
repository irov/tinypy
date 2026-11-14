"""CPython-only lazy reference for Python 2 map rows and callback recovery.

Finite values are independently checked against CPython's builtin map in
test_lazy_iterators and lazy_iterator_matrix. A class keeps callback errors
outside the source-exhaustion handler and permits a subsequent next call.
"""


class xmap(object):
    def __init__(self, function, *iterables):
        if not iterables:
            raise TypeError('xmap requires at least one iterable')
        self.function = function
        self.iterators = [iter(source) for source in iterables]
        self.exhausted = [False] * len(iterables)

    def __iter__(self):
        return self

    def next(self):
        arguments = []
        have_item = False
        for index, source in enumerate(self.iterators):
            if self.exhausted[index]:
                arguments.append(None)
                continue
            try:
                value = next(source)
            except StopIteration:
                self.exhausted[index] = True
                value = None
            else:
                have_item = True
            arguments.append(value)
        if not have_item:
            raise StopIteration
        if self.function is None:
            return arguments[0] if len(arguments) == 1 else tuple(arguments)
        return self.function(*arguments)
