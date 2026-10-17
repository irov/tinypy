# The console keeps the builtins module reachable when it destroys the VM.
# tinypy_vm_destroy finalizes only host native values, so the memoryview held
# there keeps its owner, whose __del__ would read the view.
import __builtin__


class Owner(bytearray):
    def __del__(self):
        for view in held:
            assert len(view) == 3


__builtin__.held = [memoryview(Owner("abc"))]
assert len(held[0]) == 3
