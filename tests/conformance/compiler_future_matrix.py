"""Project-authored future-flag inheritance and source-mode products."""


FEATURES = (('division', 8192), ('absolute_import', 16384),
            ('with_statement', 32768), ('print_function', 65536),
            ('unicode_literals', 131072))


def feature_flags(mask):
    return sum(flag for index, (name, flag) in enumerate(FEATURES)
               if mask & (1 << index))


def compiler_with_futures(mask):
    names = [name for index, (name, flag) in enumerate(FEATURES)
             if mask & (1 << index)]
    header = 'from __future__ import ' + ','.join(names) + '\n' if names else ''
    source = header + ('def selected(source, mode, flags, dont_inherit):\n'
                       ' return compile(source, "<future-matrix>", mode, flags, dont_inherit)\n')
    namespace = {}
    exec compile(source, '<future-owner>', 'exec', 0, 1) in namespace
    selected = namespace['selected']
    del namespace['selected']
    return selected


def shape(code):
    constants = tuple(shape(item) if type(item) is type(code) else item
                      for item in code.co_consts)
    return (code.co_argcount, code.co_nlocals, code.co_stacksize, code.co_flags,
            code.co_code.encode('hex'), constants, code.co_names, code.co_varnames,
            code.co_freevars, code.co_cellvars, code.co_filename, code.co_name,
            code.co_firstlineno, code.co_lnotab.encode('hex'))


for inherited_mask in range(32):
    selected = compiler_with_futures(inherited_mask)
    for explicit_mask in range(32):
        flags = feature_flags(explicit_mask)
        for dont_inherit in (0, 1):
            for mode in ('eval', 'exec', 'single'):
                raw = '("a",5/2)' if mode == 'eval' else 'result=("a",5/2)\n'
                for form, source in enumerate((raw, raw.decode('ascii'))):
                    code = selected(source, mode, flags, dont_inherit)
                    if mode == 'eval':
                        result = eval(code, {})
                    else:
                        namespace = {}
                        exec code in namespace
                        result = namespace['result']
                    observation = (shape(code), tuple(type(item).__name__ for item in result), result)
                    print 'future/%d/%d/%d/%s/%d\t%s' % (
                        inherited_mask, explicit_mask, dont_inherit, mode, form, repr(observation))
