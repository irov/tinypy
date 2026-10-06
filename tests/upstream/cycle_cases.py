"""Debug diagnostic adaptations of ownership-sensitive upstream fixtures.

Weak references keep cleanup possible without making the cycles reachable.
The caller supplies the CLI host's bridge to tinypy_vm_report_cycles.
"""

import _weakref


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def destructor(report):
    events = []

    def exercise():
        class DelTest:
            def __del__(self):
                events.append('finalized')
        value = DelTest()
        del value

    exercise()
    check(events == ['finalized'], 'acyclic destructor did not run')
    check(report() == 0, 'detector reported an acyclic destructor fixture')


def reversed_cycle(report):
    def create():
        class Seq:
            def __len__(self):
                return 10
            def __getitem__(self, index):
                return index
        sequence = Seq()
        iterator = reversed(sequence)
        sequence.r = iterator
        check(report() == 0, 'reachable reversed cycle was reported as unreachable')
        return _weakref.ref(sequence)

    reference = create()
    try:
        check(report() == 1, 'detector missed the reversed/sequence cycle')
    finally:
        sequence = reference()
        if sequence is not None:
            del sequence.r
        sequence = None
    check(reference() is None, 'sequence survived cycle cleanup')
    check(report() == 0, 'reversed cycle remained after cleanup')


def augmented_assignment_cycle(report):
    def create():
        class Copying:
            def __init__(self, value):
                self.val = value
            def __add__(self, value):
                return Copying(self.val + value)

        class Mutating(Copying):
            def __iadd__(self, value):
                self.val += value
                return self

        class Replacing(Copying):
            def __iadd__(self, value):
                return Replacing(self.val + value)

        for constructor, same in ((Copying, False), (Mutating, True), (Replacing, False)):
            value = constructor(1)
            previous = value
            value += 10
            check((value is previous) == same, 'augmented assignment identity changed')
            check(value.val == 11, 'augmented assignment value changed')
        check(report() == 0, 'reachable class closures were reported as unreachable')
        return _weakref.ref(Copying), _weakref.ref(Replacing)

    references = create()
    try:
        check(report() == 2, 'detector missed class/method/closure cycles')
    finally:
        copying, replacing = [reference() for reference in references]
        if copying is not None:
            del copying.__add__
        if replacing is not None:
            del replacing.__iadd__
        copying = replacing = None
    check(all(reference() is None for reference in references), 'classes survived cycle cleanup')
    check(report() == 0, 'closure cycles remained after cleanup')


def recursive_callable_cycle(report):
    def create():
        class Relay:
            pass
        Relay.__call__ = Relay()
        try:
            Relay()()
        except RuntimeError:
            pass
        else:
            raise AssertionError('recursive classic callable did not raise RuntimeError')
        check(report() == 0, 'reachable class/instance cycle was reported as unreachable')
        return _weakref.ref(Relay)

    reference = create()
    try:
        check(report() == 1, 'detector missed the class/instance cycle')
    finally:
        relay = reference()
        if relay is not None:
            del relay.__call__
        relay = None
    check(reference() is None, 'class survived cycle cleanup')
    check(report() == 0, 'callable cycle remained after cleanup')


CASES = {
    'cpython.test_class.ClassTests.testDel': destructor,
    'cpython.test_enumerate.TestReversed.test_gc': reversed_cycle,
    'cpython.test_augassign.AugAssignTest.testCustomMethods1': augmented_assignment_cycle,
    'cpython.test_class.ClassTests.testSFBug532646': recursive_callable_cycle,
}


def run(identity, report):
    CASES[identity](report)
