# Shared deterministic workload for TinyPy CLI and Python 2.7.
import sys


def speed_stress(count):
    ring = [0] * 4096
    table = {}
    state = 1
    checksum = 0

    for index in xrange(count):
        state = (state * 1103515245 + 12345) & 0x7fffffff
        slot = index & 4095
        ring[slot] = state
        table[slot] = state ^ index
        if (index & 31) == 0:
            checksum = (checksum + table.get(slot, 0) + ring[(slot - 17) & 4095]) & 0x7fffffff

    populated = 4096 if count >= 4096 else count
    for index in xrange(populated):
        checksum = (checksum ^ ring[index] ^ table[index]) & 0x7fffffff

    return checksum, len(table), len(ring)


def call_step(state, index):
    return ((state * 33) ^ index ^ (state >> 7)) & 0x7fffffff


def call_stress(count):
    state = 1

    for index in xrange(count):
        state = call_step(state, index)

    return state, count


class AttributeCounter(object):
    def __init__(self):
        self.value = 1
        self.mask = 0x7fffffff

    def step(self, index):
        self.value = ((self.value * 33) ^ index ^ (self.value >> 7)) & self.mask
        return self.value


def attribute_stress(count):
    counter = AttributeCounter()
    checksum = 0

    for index in xrange(count):
        checksum = (checksum + counter.step(index)) & counter.mask

    return checksum, counter.value


def compiler_stress(count):
    source = (
        "def generated(value):\n"
        "    total = 0\n"
        "    for item in xrange(value):\n"
        "        total += item\n"
        "    return total\n"
    )
    checksum = 0

    for index in xrange(count):
        code = compile(source, "<compiler-stress>", "exec")
        checksum = (checksum + len(code.co_code) + len(code.co_consts) + len(code.co_names) + code.co_stacksize + index) & 0x7fffffff

    return checksum, count


def churn_stress(count):
    width = 256
    checksum = 0

    for batch in xrange(count):
        rows = []
        lookup = {}
        base = batch * width

        for offset in xrange(width):
            value = base + offset
            row = (value, value * 3, "row-%d" % value)
            rows.append(row)
            lookup[offset] = row

        checksum = (checksum + rows[0][0] + rows[-1][1] + len(lookup[width - 1][2])) & 0x7fffffff

    return checksum, count * width


def memory_stress(count):
    rows = []
    lookup = {}
    checksum = 0

    for index in xrange(count):
        label = "item-%d" % index
        row = (index, index * 3, label)
        rows.append(row)
        lookup[index] = row

    stride = count // 97
    if stride == 0:
        stride = 1
    for index in xrange(0, count, stride):
        row = lookup[index]
        checksum = (checksum + row[0] + row[1] + len(row[2])) & 0x7fffffff

    return checksum, len(rows), len(lookup)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("speed", "calls", "attributes", "compiler", "churn", "memory"):
        raise SystemExit("usage: runtime_stress.py speed|calls|attributes|compiler|churn|memory count")

    mode = sys.argv[1]
    count = int(sys.argv[2])
    if count < 0:
        raise SystemExit("count must be non-negative")

    if mode == "speed":
        checksum, table_size, ring_size = speed_stress(count)
        print "speed checksum=%d table=%d ring=%d" % (checksum, table_size, ring_size)
    elif mode == "calls":
        checksum, call_count = call_stress(count)
        print "calls checksum=%d calls=%d" % (checksum, call_count)
    elif mode == "attributes":
        checksum, value = attribute_stress(count)
        print "attributes checksum=%d value=%d count=%d" % (checksum, value, count)
    elif mode == "compiler":
        checksum, compile_count = compiler_stress(count)
        print "compiler checksum=%d compilations=%d" % (checksum, compile_count)
    elif mode == "churn":
        checksum, object_count = churn_stress(count)
        print "churn checksum=%d objects=%d" % (checksum, object_count)
    else:
        checksum, row_count, lookup_size = memory_stress(count)
        print "memory checksum=%d rows=%d lookup=%d" % (checksum, row_count, lookup_size)


if __name__ == "__main__":
    main()
