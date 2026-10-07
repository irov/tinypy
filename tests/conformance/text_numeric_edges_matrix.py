"""Project-authored bounded numeric, text and formatting edge domains.

Every row has a stable unique identity and is compared with CPython 2.7.18.
"""

def numeric_result(identity, function, *args):
    try:
        value = function(*args)
        result = value.hex() if isinstance(value, float) else repr(value)
        print identity + '\t' + type(value).__name__ + ':' + result
    except Exception as error:
        print identity + '\t' + 'error:' + type(error).__name__

texts = ('', '0','00','08','010','0o10','0b10','0x10','0Xff','-0x80',
       '+ 12','- 12','+\t12','-\n12','0x ff','0b 1','1L','1l','01L',
       '0x10L','+0x10L','-0x10L','1_0','1\0','1\0x',' 1\x85',
       u'\u0661\u0662',u'\uff11\uff12',u'\u200312\u2003',u'\u00a012\u00a0',
       '2.2250738585072011e-308','2.4703282292062327e-324',
       '2.4703282292062328e-324','1.7976931348623158e308',
       '1.7976931348623159e308','inf','-NaN','Infinity','nan(payload)')
for i, text in enumerate(texts):
    for j, base in enumerate((0,2,8,10,16,36)):
        numeric_result('parse-int-%d-%d' % (i,j),int,text,base)
        numeric_result('parse-long-%d-%d' % (i,j),long,text,base)
    numeric_result('parse-float-%d' % i,float,text)

hextexts = (u'\u0661',u'\uff11',u'\u20030x1p1',u'\u00a00x1p1',u'\ud800',
          u'\u00e9','0x1.fffffffffffff7ffffffffp1023',
          '0x1.fffffffffffff8p1023','0x1.00000000000008p0',
          '0x1.000000000000080001p0','0x0.fffffffffffff8p-1022',
          '-0x0.00000000000008p-1022','  -0XABC.DEFp-99\t',
          '1p9999999999999999999999','0p9999999999999999999999',
          '1p-9999999999999999999999','1'*1000+'p-4000',
          '0x.'+'0'*999+'8p3000')
for i, text in enumerate(hextexts):
    numeric_result('fromhex-%d' % i,float.fromhex,text)

values = (0.0,-0.0,.125,.15,.25,.35,.45,.55,1.005,2.675,
        56294995342131.5,1e308,2.0**-1074,float('inf'),float('nan'))
for i, value in enumerate(values):
    for j, digits in enumerate((-400,-309,-308,-100,-2,-1,0,1,2,3,22,23,307,308,323,324,400)):
        numeric_result('round-%d-%d' % (i,j),round,value,digits)

class Integer(int):
    def __float__(self):
        return 1.25
class Long(long):
    def __float__(self):
        return 1.25
class Float(float):
    def __float__(self):
        raise AssertionError('ignored')
for i, value in enumerate((Integer(8),Long(8),Float(1.25))):
    numeric_result('round-subtype-%d' % i,round,value,1)


def emit(identity, function, *args):
    try:
        value = function(*args)
        print identity + '\t' + type(value).__name__ + ':' + repr(value)
    except Exception as error:
        print identity + '\t' + 'error:' + type(error).__name__

texts = ('', 'aaaa','ababa','a\0b',' \ta\nb\r ',u'',u'aaaa',u'\u00a0a\u2003b\u0085',u'\U0001f600a\U0001f600', '\xffa')
separators = ('', 'a','aa','ab','\0',u'',u'a',u'\U0001f600',u'\u2003',None,1)
for i, text in enumerate(texts):
    for j, separator in enumerate(separators):
        for name in ('partition','rpartition','strip','lstrip','rstrip'):
            emit('text-%d-%d-%s' % (i,j,name),getattr(text, name),separator)
        for k, count in enumerate((-2,-1,0,1,2,5)):
            for name in ('split','rsplit'):
                emit('text-%d-%d-%d-%s' % (i,j,k,name),getattr(text, name),separator,count)
            emit('text-%d-%d-%d-replace' % (i,j,k),text.replace,separator,u'X',count)

class S(str):
    def __str__(self):
        return 'override'
    def __unicode__(self):
        return u'override'
class U(unicode):
    def __str__(self):
        return 'override'
    def __unicode__(self):
        return u'override'
for i, text in enumerate((S('abc'),U(u'abc'))):
    for j, separator in enumerate((S('b'),U(u'b'))):
        for name in ('partition','rpartition'):
            result = getattr(text, name)(separator)
            print 'partition-sub-%d-%d-%s\t' % (i,j,name) + repr(([type(v).__name__ for v in result], result[1] is separator, result))
    for name, args in (('strip',()),('replace',('z','x')),('split',()),('join',(['ab'],))):
        emit('sub-%d-%s' % (i,name),getattr(text, name),*args)

for i, sequence in enumerate((['a',1,'b'],['\xff',u'x',1],['\xff',u'x'],[u'x','\xff',1])):
    for j, separator in enumerate(('',u'',S(''),U(u''))):
        events = []
        def items():
            for k, value in enumerate(sequence):
                events.append(k)
                yield value
            events.append('end')
        emit('join-generator-%d-%d' % (i,j),separator.join,items())
        print 'join-events-%d-%d\t' % (i,j) + repr(events)

class L(list):
    def __iter__(self):
        return iter(('replacement',))
class T(tuple):
    def __iter__(self):
        return iter(('replacement',))
for i, sequence in enumerate((L(('a','b')),T(('a','b')))):
    emit('join-subtype-%d' % i,','.join,sequence)

for i, text in enumerate(('abc',u'abc','\xff',u'\u00e9')):
    for j, candidate in enumerate((1,u'x','x',u'\u00e9','\xff',())):
        for name in ('find','count','startswith','split','replace'):
            events = []
            class Bound(object):
                def __index__(self):
                    events.append('index')
                    raise KeyError('bound')
                def __int__(self):
                    events.append('int')
                    raise KeyError('bound')
            args = (candidate,Bound())
            if name == 'replace':
                args = (candidate,'y',Bound())
            emit('order-%d-%d-%s' % (i,j,name),getattr(text, name),*args)
            print 'events-%d-%d-%s\t' % (i,j,name) + repr(events)


values = (0,1,-1,1L<<200,0.0,-0.0,.125,1e-7,1e20,float('inf'),float('nan'),
        complex(-0.0,-0.0),complex(1e-7,1e20),complex(-1e20,-1e-7),'abcd',u'\u20ac')
specs = ('*>+16,.3f','0=+18,.3f','^24.7g','*^+24.7G','>#20x','0=+#20x',
       '>20.12','>20.13',',.12','#,','%.0','.0%','+.0%','020,.0f','0=020,.0f',
       '_^20s','\0^10',u'\u20ac^12',u'\U0001f600^12','*>20.1%',
       '0=30,.0f','0=30,.0g','0=30,.0','0=30,.3','0=30,.3f')
for i, value in enumerate(values):
    for j, spec in enumerate(specs):
        emit('format-edge-%d-%d' % (i,j),format,value,spec)
    for j, precision in enumerate((0,1,3,17)):
        emit('format-nested-%d-%d' % (i,j),'{0:>{1}.{2}f}'.format,value,24,precision)

class Value(object):
    def __format__(self, spec):
        return 'bytes'
class UnicodeValue(object):
    def __format__(self, spec):
        return u'\u20ac'
class BadValue(object):
    def __format__(self, spec):
        return 1
for i, value in enumerate((Value(),UnicodeValue(),BadValue())):
    for j, text in enumerate(('{0}','{0!s}','{0!r}',u'{0}',u'{0!s}',u'{0!r}')):
        if '!s' not in text and '!r' not in text:
            emit('format-protocol-%d-%d' % (i,j),text.format,value)
    for j, spec in enumerate(('',u'',u'\u20ac')):
        emit('format-protocol-builtin-%d-%d' % (i,j),format,value,spec)
