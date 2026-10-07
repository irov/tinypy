"""Finite Python 2 type/slots protocol oracle matrix: 415 unique rows.

Run independently under CPython 2.7.18 and tinypy; output includes outcomes,
exception diagnostics and observable callback order. No subprocesses or
implementation-derived expected values are used by this fixture.
"""

# slots_probe
def observe(identity, function, *args):
    try:
        print identity + '\t' + repr(function(*args))
    except Exception as error:
        print identity + '\t' + repr(('error', type(error).__name__, str(error)))

class Plain(object):
    pass
class Empty(object):
    __slots__ = ()
class Slot(object):
    __slots__ = ('base',)
class Old:
    pass

factories = (
    lambda: (), lambda: [], lambda: None, lambda: 'x', lambda: u'x',
    lambda: ('x',), lambda: ('x', 'x'), lambda: ('z', 'a'),
    lambda: ('__dict__',), lambda: ('__weakref__',),
    lambda: ('__dict__', '__dict__'), lambda: ('__weakref__', '__weakref__'),
    lambda: ('',), lambda: ('1abc',), lambda: ('a b',), lambda: ('a.b',),
    lambda: ('\xff',), lambda: (u'\u00e9',), lambda: ('x\x00y',),
    lambda: ('__x',), lambda: (1,), lambda: {'x': 1, 'a': 2},
    lambda: iter(('x', 'a')), lambda: (item for item in ('x', 'a')),
)
bases = ((object,), (Plain,), (Empty,), (Slot,), (int,), (long,), (float,),
         (str,), (unicode,), (tuple,), (list,), (dict,), (type,))

def build(factory, base):
    declaration = factory()
    result = type('_Name', base, {'__slots__': declaration})
    namespace = result.__dict__
    members = sorted((name, type(value).__name__) for name, value in namespace.items()
                     if not name.startswith('__') or name.startswith('_Name'))
    return ('ok', members, hasattr(result, '__dict__'), result.__slots__ is declaration)

for i, factory in enumerate(factories):
    for j, base in enumerate(bases):
        observe('slots/%d/%d' % (i,j), build, factory, base)

for i, declaration in enumerate((('x',), ('__x',), ('__dict__',), ('__weakref__',))):
    for j, namespace in enumerate(({'x': 1}, {'_Name__x': 1}, {'__x': 1}, {'__dict__': 1}, {'__weakref__': 1})):
        def collision():
            values = dict(namespace)
            values['__slots__'] = declaration
            result = type('_Name', (object,), values)
            return sorted((name,type(value).__name__) for name,value in result.__dict__.items())
        observe('collision/%d/%d' % (i,j), collision)


# type_calls_probe
def show(identity, function, *args, **kwargs):
    try:
        value=function(*args,**kwargs)
        print identity+'\t'+repr(('ok',value.__name__ if isinstance(value,type) else value))
    except Exception as error:
        print identity+'\t'+repr(('error',type(error).__name__,str(error)))
for i,args in enumerate(((),(1,),('C',(),{}),('C',),('C',()),('C',(),{},1),(u'C',(),{}),(u'\u00e9',(),{}))):
    show('type/%d'%i,type,*args)
    show('new/%d'%i,type.__new__,type,*args)
    show('init/%d'%i,type.__init__,type,*args)
for i,args in enumerate(((1,),('C',(),{}))):
    show('typekw/%d'%i,type,*args,extra=1)
    show('newkw/%d'%i,type.__new__,type,*args,extra=1)
    show('initkw/%d'%i,type.__init__,type,*args,extra=1)


# extra_probe
import copy_reg
import _weakref
def show(identity, function):
    try:
        value=function()
        print identity+'\t'+repr(('ok',value))
    except Exception as error:
        print identity+'\t'+repr(('error',type(error).__name__,str(error)))

events=[]
class Meta(type):
    def __new__(meta,*args,**kwargs):
        events.append(('new',args[0] if args else None,len(args),sorted(kwargs)))
        return type.__new__(meta,*args,**kwargs)
    def __init__(cls,*args,**kwargs):
        events.append(('init',args[0] if args else None,len(args),sorted(kwargs)))
class Base(object):
    __metaclass__=Meta
events[:]=[]
def winner():
    C=type(name='C',bases=(Base,),dict={})
    return type(C).__name__,events[:]
show('winnerkw',winner)
for label,args,kwargs in [('allkw',(),dict(name='C',bases=(),dict={})),('mixed',('C',),dict(bases=(),dict={})),('duplicate',('C',()),dict(name='D')),('unknown',('C',()),dict(extra=0)),('none',('C',None,{}),{})]:
    show('newkw/'+label,lambda args=args,kwargs=kwargs:type.__new__(type,*args,**kwargs).__name__)
    show('typekw/'+label,lambda args=args,kwargs=kwargs:type(*args,**kwargs).__name__)
for base in (object,int,long,float,str,unicode,tuple,list,dict):
    for declaration in (None,(),('x',)):
        def weak(base=base,declaration=declaration):
            namespace={} if declaration is None else {'__slots__':declaration}
            C=type('C',(base,),namespace)
            value=C()
            try:
                ref=_weakref.ref(value)
                result=True
            except TypeError:
                result=False
            return result,hasattr(value,'__dict__'),hasattr(C,'__weakref__')
        show('weak/'+base.__name__+'/'+repr(declaration),weak)
def mutating():
    namespace={}
    class Slots(object):
        def __iter__(self):
            events.append('iter')
            namespace['marker']=7
            return iter(('x',))
        def __len__(self):
            events.append('len')
            namespace['length_marker']=8
            return 1
    namespace['__slots__']=Slots()
    events[:]=[]
    C=type('C',(object,),namespace)
    result=getattr(C,'marker',None),getattr(C,'length_marker',None),events[:]
    namespace.clear()
    del C.__slots__
    return result
show('mutating',mutating)
for declaration in ({'x':1,'a':2},iter(('x','a')),(x for x in ('x','a')),['x','__p','__dict__','__weakref__']):
    def slotnames(declaration=declaration):
        C=type('C',(object,),{'__slots__':declaration})
        return copy_reg._slotnames(C)
    show('slotnames/'+type(declaration).__name__,slotnames)
class Names(list):
    def __iter__(self):
        events.append('iter')
        return iter(('x','a'))
    def __len__(self):
        events.append('len')
        return 2
def names():
    C=type('C',(object,),{'__slots__':Names(['x'])})
    events[:]=[]
    return copy_reg._slotnames(C),events[:]
show('nameshooks',names)
for left,right in [(('a','z'),('z','a')),(['a','z'],['a','z']),(('a','z'),['a','z']),(('x','x'),('x','x'))]:
    def assign(left=left,right=right):
        A=type('A',(object,),{'__slots__':left})
        B=type('B',(object,),{'__slots__':right})
        a=A()
        a.__class__=B
        return a.__class__.__name__
    show('classassign/'+repr((left,right)),assign)


# order_probe
events=[]
def show(identity, function):
    events[:]=[]
    try:
        value=function()
        print identity+'\t'+repr(('ok',value,events[:]))
    except Exception as error:
        print identity+'\t'+repr(('error',type(error).__name__,str(error),events[:]))
class Name(str):
    def __lt__(self,other):
        events.append(('lt',str(self),str(other)))
        return str(self)<str(other)
    def __eq__(self,other):
        events.append(('eq',str(self),str(other)))
        return str(self)==str(other)
    def __cmp__(self,other):
        events.append(('cmp',str(self),str(other)))
        return cmp(str(self),str(other))
    def __hash__(self):
        return hash(str(self))
show('sort',lambda:sorted(name for name in type('C',(object,),{'__slots__':(Name('z'),Name('a'))}).__dict__ if not name.startswith('__')))
class X(object):pass
class Y(object):pass
class A(X,Y):pass
class B(Y,X):pass
class Slots(object):
    def __iter__(self):
        events.append('iter')
        return iter(('x',))
    def __len__(self):
        events.append('len')
        return 1
show('mroorder',lambda:type('C',(A,B),{'__slots__':Slots()}).__name__)
show('nulorder',lambda:type('C\0',(object,),{'__slots__':Slots()}).__name__)
class Subtuple(tuple):
    def __iter__(self):
        events.append('bases-iter')
        return iter((int,))
    def __len__(self):
        events.append('bases-len')
        return 1
    def __getitem__(self,key):
        events.append('bases-getitem')
        return int
show('bases-subtuple',lambda:type('C',Subtuple((object,)),{}).__base__.__name__)
class Subdict(dict):
    def __iter__(self):
        raise AssertionError('iter')
    def keys(self):
        raise AssertionError('keys')
    def __getitem__(self,key):
        raise AssertionError('getitem')
show('namespace-subdict',lambda:type('C',(object,),Subdict(marker=4)).marker)
