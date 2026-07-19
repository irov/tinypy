events = []
saved = []


class Finalized(object):
    def __init__(self, name, resurrect=False):
        self.name = name
        self.resurrect = resurrect

    def __del__(self):
        events.append(self.name)
        if self.resurrect:
            self.resurrect = False
            saved.append(self)


value = Finalized("normal")
del value
assert events == ["normal"]

value = Finalized("resurrected", True)
del value
assert events == ["normal", "resurrected"]
assert len(saved) == 1
value = saved.pop()
del value
assert events == ["normal", "resurrected", "resurrected"]
