"""Bounded nested statement and suspended-unwind products; CPython is the oracle."""


WRAPPERS = ('plain', 'if', 'while', 'for', 'except', 'finally', 'with', 'multiwith')
ACTIONS = ('record', 'return', 'break', 'continue', 'raise', 'yield')


def indent(source):
    return ''.join(' ' + line if line.strip() else line
                   for line in source.splitlines(True))


def wrap(body, kind, depth):
    if kind == 'plain':
        return body
    if kind == 'if':
        return 'if flag:\n' + indent(body) + 'else:\n events.append("if-else")\n'
    if kind == 'while':
        counter = 'remaining%d' % depth
        return ('%s=2\nwhile flag and %s:\n %s-=1\n' % (counter, counter, counter) +
                indent(body) + 'else:\n events.append("while-else")\n')
    if kind == 'for':
        return ('for item%d in (0,1):\n' % depth + indent(body) +
                'else:\n events.append("for-else")\n')
    if kind == 'except':
        return ('try:\n' + indent(body) +
                'except ValueError, error:\n events.append(("except",error.args))\n'
                'else:\n events.append("try-else")\n')
    if kind == 'finally':
        return 'try:\n' + indent(body) + 'finally:\n events.append("finally")\n'
    if kind == 'with':
        return 'with Context(events,suppress,"one"):\n' + indent(body)
    if kind == 'multiwith':
        return ('with Context(events,suppress,"one"), Context(events,suppress,"two"):\n' +
                indent(body))
    raise AssertionError(kind)


class Context(object):
    def __init__(self, events, suppress, name):
        self.events = events
        self.suppress = suppress
        self.name = name
    def __enter__(self):
        self.events.append(('enter', self.name))
    def __exit__(self, kind, value, traceback):
        self.events.append(('exit', self.name, None if kind is None else kind.__name__))
        return self.suppress


def outcome(callback):
    try:
        return ('value', callback())
    except BaseException as error:
        return ('error', type(error).__name__, error.args)


for outer in WRAPPERS:
    for inner in WRAPPERS:
        for action in ACTIONS:
            statements = {'record': 'pass\n', 'return': 'return 7\n',
                          'break': 'break\n', 'continue': 'continue\n',
                          'raise': 'raise ValueError("body",step)\n', 'yield': 'yield step\n'}
            body = 'events.append(("body",step))\n' + statements[action]
            source = ('def controlled(flag,suppress,events):\n for step in (0,1):\n' +
                      indent(indent(wrap(wrap(body, inner, 1), outer, 0))) +
                      ' events.append("end")\n return\n')
            namespace = {'Context': Context}
            exec compile(source, 'control-product', 'exec', 0, 1) in namespace
            controlled = namespace['controlled']
            # The function owns its globals; avoid a function/globals ownership cycle.
            del namespace['controlled']
            for flag in (False, True):
                for suppress in (False, True):
                    drivers = ('drain', 'throw', 'close') if action == 'yield' else ('call',)
                    for driver in drivers:
                        events = []
                        if driver == 'call':
                            result = outcome(lambda: controlled(flag, suppress, events))
                            final_state = None
                        else:
                            generator = controlled(flag, suppress, events)
                            steps = []
                            if driver != 'drain':
                                steps.append(outcome(generator.next))
                                if driver == 'throw':
                                    steps.append(outcome(lambda: generator.throw(ValueError('driver'))))
                                else:
                                    steps.append(outcome(generator.close))
                            terminal = False
                            for unused in range(16):
                                step_result = outcome(generator.next)
                                steps.append(step_result)
                                if step_result[0] == 'error':
                                    terminal = True
                                    break
                            if not terminal:
                                raise AssertionError('bounded generator failed to terminate')
                            steps.append(outcome(generator.close))
                            result = steps
                            final_state = (generator.gi_frame is None, generator.gi_running)
                        print 'control/%s/%s/%s/%s/%s/%s\t%s' % (
                            outer, inner, action, int(flag), int(suppress), driver,
                            repr((result, events, final_state)))
