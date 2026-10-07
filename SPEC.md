# tinypy: спецификация runtime и compiler

## 1. Назначение

tinypy — встраиваемая реализация Python 2.7 для выполнения доверенного и
ограниченного кода из memory buffers. Runtime, compiler и форматы данных
реализованы на C11 и не требуют установленного Python.

Core не обращается напрямую к:

- filesystem и стандартным потокам;
- environment и process API;
- locale;
- sockets;
- thread API и TLS;
- системному allocator.

Память, modules, output, diagnostics и interrupt polling предоставляет host
через C callbacks.

## 2. Границы реализации

Поддерживаются:

- Python 2.7 source semantics;
- Python 2.7 bytecode и code objects;
- marshal v2;
- режимы компиляции `exec`, `eval` и `single`;
- динамические `compile`, `eval` и строковый `exec`;
- host-resolved source и bytecode modules;
- независимые VM без общей runtime-блокировки.

Не поддерживаются:

- Python 3;
- CPython binary extension ABI;
- загрузка shared libraries из core;
- Python-visible `_ast` и `compile(ast, ...)`;
- одновременное выполнение одной VM несколькими потоками;
- автоматический cyclic GC.

## 3. C ABI

Публичный namespace:

```text
tinypy_          functions, types and values
TINYPY_          constants and enum values
tinypy_internal_ cross-unit implementation helpers
__tinypy_        private implementation functions and tables
```

Правила именования:

- структуры, union и scalar typedef заканчиваются на `_t`;
- enum typedef заканчивается на `_e`;
- declarations в публичных headers записываются в одну строку;
- `tinypy.h` агрегирует весь C API;
- `tinypy.hpp` является единственным C++ proxy header;
- публичные C headers не содержат `extern "C"` и `__cplusplus`.

ABI-extensible структуры начинаются с:

```c
uint32_t abi_version;
uint32_t struct_size;
```

Меньший известный `struct_size` означает отсутствие добавленных в конец полей.
Required pointers, ownership, правильный direct-accessor type и индекс являются
C preconditions. Их нарушение имеет undefined behavior во всех build types;
публичный контракт не обещает runtime-проверку или debug assertion.

## 4. VM и параллельность

`tinypy_vm_t` владеет всем mutable state:

- builtins и modules;
- interned values и cached constants;
- frames и exception state;
- compiler context;
- import state;
- recursion и instruction counters;
- allocator и host callbacks.

Process-wide mutable globals, GIL и TLS отсутствуют. Неизменяемые opcode,
grammar, Unicode и parser tables могут разделяться всеми VM.

Одна VM имеет одного owner thread. Синхронный reentrant вход из native callback
разрешён. Разные VM могут выполняться и компилировать одновременно.

## 5. Память и lifetime

Каждый runtime object начинается с универсального header:

```c
struct tinypy_value_t {
    tinypy_ref_t ref;
    tinypy_type_t *type;
};
```

Variable-size objects добавляют signed size и type-specific storage. Общий
payload union и allocation prefix отсутствуют.

Вся внешняя память запрашивается только через `tinypy_allocator_t` с размером,
alignment и user data. NULL восстанавливается как `MemoryError` только в
явно fallible операциях с вычисляемым размером; остальные корректные allocation
requests обязаны вернуть non-NULL.

Runtime allocations размером до 512 bytes обслуживает VM-local pool allocator:

- size classes кратны native alignment;
- pools имеют размер 4 KiB;
- arenas имеют размер 256 KiB;
- частично свободные arenas упорядочены от наиболее заполненной;
- полностью свободная arena немедленно возвращается host allocator;
- allocations больше 512 bytes передаются host allocator напрямую.

Pool state принадлежит конкретной `tinypy_vm_t`; process globals, TLS и locks
для него не используются. Host видит реальные внешние allocations, а не каждый
логический pooled object. При budget preflight `max_heap_bytes` учитывает VM
object, arena table, полный зарезервированный размер arenas и все прямые
allocations. Вычисляемые результаты `long`, text, buffer, list и tuple
отклоняются с `MemoryError`, если они не помещаются в остаток budget.

Lifetime определяется reference counting. Ациклические объекты уничтожаются
немедленно. Host обязан освободить owned references и разорвать owning cycles
перед `tinypy_vm_destroy`.

При уничтожении module его сохранённый снаружи словарь сохраняет ключи;
значения byte-string ключей заменяются на `None` в два прохода: сначала имена
с одним начальным `_`, затем остальные, кроме `__builtins__`. Нестроковые и
Unicode ключи сохраняются. Traversal для диагностики циклов только читает
ссылки и не очищает словарь. Python `module.__new__` создаёт объект без имени
и словаря до инициализации; C accessors возвращают borrowed NULL для этих
отсутствующих полей. Добавление значения и использование модуля как builtins
создают словарь лениво.

Это основополагающее правило, а не отсутствующая возможность: runtime не ведёт
реестр живых значений, а `tinypy_vm_destroy` не выполняет sweep недостижимых
объектов. Значение, живое к этому моменту, — ошибка host или runtime, и
исправляется в месте утечки ссылки; для поиска таких мест предназначены opt-in
cycle diagnostics.

`tinypy_vm_destroy` сначала вызывает финализаторы native functions и native
instances, достижимых из VM, пока все значения живы, и только затем
освобождает память достижимых объектов. Финализатор вызывается ровно один раз.

Модуль `sys` принадлежит VM через собственную ссылку: `sys.stdout`,
`sys.displayhook` и `sys.exc_*` читаются и пишутся напрямую в его словарь, а
переопределение `sys.modules['sys']` на рантайм не влияет. Атрибут `__mro__`
возвращает владеющую копию внутреннего кортежа MRO: тип не может владеть
кортежем, ссылающимся на него самого, без циклического сборщика.

VM хранит собственные постоянные значения:

- `None`, `True`, `False`;
- integers от `-1023` до `1024`;
- positive float zero;
- пустую byte string;
- пустой tuple.

Они имеют обычный refcount и базовую owned reference самой VM; специального
immortal refcount sentinel нет.

## 6. Object model

Runtime реализует:

- `None`, bool, integer, long, float и complex;
- byte `str` и `unicode`;
- tuple, list, dict, set, frozenset, bytearray и buffer;
- slices и sequence/mapping protocols;
- functions, bound methods, closures и cells;
- iterators, comprehensions и generators;
- exceptions, frames и tracebacks;
- old-style и new-style classes, включая classic classes среди `__bases__` и
  `__mro__` new-style класса (`tinypy_type_mro_at` и `tinypy_type_base_at`
  возвращают NULL для classic записей);
- `type`, metaclasses, C3 MRO, custom `mro()` и `super`;
- descriptors, properties, class/static methods и `__slots__`;
- weak references и explicit finalization behavior.

Type slots возвращают прямой semantic result. Неверный slot input нарушает C
precondition и имеет undefined behavior. Python exceptions используются только
для настоящих runtime ошибок.

Внутренний MRO типа заимствует ссылку на сам тип и владеет остальными
записями, включая классы, добавленные custom `mro()` вне `__bases__`.
Без cyclic GC владеющая ссылка на сам тип образовала бы неосвобождаемый цикл.
`__mro__` при каждом обращении возвращает новый кортеж с владеющими ссылками,
и `C.__mro__ is C.__mro__` ложно, в отличие от CPython. Hook вызывается при
создании типа и пересчёте MRO потомков после изменения баз; результат должен
содержать классы с совместимым solid layout. Пересекающиеся реентерабельные
присваивания `__bases__` во время hook отвергаются с RuntimeError; неудачный
пересчёт откатывает MRO и базы. Member и getset descriptors, пережившие свой
тип, остаются безопасными: repr показывает `<deleted type>`, а применение к
объекту даёт TypeError.

Runtime `intern()`, имена compiler и interned строки marshal используют общую
VM-local таблицу без владеющих ссылок. Строка удаляется из таблицы при переходе
refcount в ноль до отложенного освобождения; таблица периодически очищает
tombstones и сокращает ёмкость. Односимвольные строки и другие постоянные
значения по-прежнему удерживаются своими owned references самой VM.

Обычный и checked конструкторы byte strings переиспользуют уже interned
строку по байтам и длине и возвращают владеющую ссылку; новые строки при
промахе не добавляются в intern table автоматически. Пустая строка и строки
длины один сохраняют отдельные VM constant caches. Lookup не удерживает
удалённые строки, учитывает embedded NUL и не переиспользует объекты при
shutdown. Длинные строки, превышающие верхнюю границу длины записей таблицы,
не требуют вычисления hash при lookup.

Постоянные имена протоколов, метаданных, keyword-аргументов, кодировок и
служебных объектов компилятора создаются один раз в общем реестре VM.
Lookup по байтам и длине переиспользует соответствующий пресет; внутренний
name factory возвращает владеющую ссылку. Реестр задаёт поля, инициализацию,
признак interning и shutdown roots, поэтому строки разных VM не смешиваются.
Имена `<genexpr>`, `<setcomp>` и `<dictcomp>` остаются non-interned, сохраняя
побайтовое совпадение marshal с CPython 2.7.18.

Реестр фиксированных имён задан непосредственно в исходниках. Идентификаторы
из AST, в том числе generated names meta frontend, добавляются в общую intern
table при создании; имена и подходящие строковые константы code objects
интернируются по правилам Python 2. Данные обычных runtime strings туда
не добавляются без явного `intern()`. Отдельные non-interned compiler labels
не используются как объекты AST identifiers даже при совпадении текста.

`tinypy_type_get_attr` и `tinypy_type_set_attr` принимают байты и длину;
варианты `*_attr_key` принимают готовый borrowed str/unicode key той же VM.
Setters сохраняют одинаковую регистрацию native method descriptors, включая
owner и descriptor kind. `tinypy_native_function_new_key` сохраняет готовое
byte-string имя с владеющей ссылкой и получает VM из него. Эти key API не
создают строк, не ищут имена по байтам и не добавляют ключи в intern table.

`tinypy_module_new_key`, `tinypy_type_new_key` и `tinypy_native_type_new_key`
принимают borrowed byte-string имя, получают VM из него и сохраняют именно
этот объект с владеющей ссылкой. `tinypy_module_add_value_key` и
`tinypy_module_get_value_key` принимают borrowed str/unicode key той же VM;
getter возвращает borrowed value или NULL. Запись native function сохраняет
его module metadata по тем же правилам, что и байтовый API.
`tinypy_instance_get_attr_key` возвращает borrowed значение непосредственно
из instance dictionary либо type dictionary, без descriptor binding.
`tinypy_instance_set_attr_key` пишет прямо в instance dictionary, без вызова
пользовательского `__setattr__` или descriptor setter. Для Python attribute
semantics используются `tinypy_object_*_attr_value`. Embedded NUL учитывается
полной длиной key; существующие byte APIs остаются совместимыми адаптерами.
`tinypy_import_module_key` принимает borrowed byte-string имя; обработка
составных имён и host resolver используют его байтовое представление.

Все внутренние операции с именами модулей, атрибутов, специальных методов и
именованных аргументов используют value keys. Фиксированные таблицы содержат
borrowed VM keys либо offsets этих полей для static callback data. Сравнения
имён AST в meta/preprocessor также используют готовые interned keys.
Проверка исходников запрещает внутренние вызовы byte-name API и создание фиксированных
ключей через byte factory. Байты остаются на публичных/host границах, в
обработке динамических путей импорта, parser tokens и текстах диагностики.
Начальный bootstrap builtin types хранит C names до создания string type
и VM presets; такие имена не создают Python strings.

Внутренняя регистрация native functions, methods, classmethods, staticmethods
и properties использует общие `tinypy_internal_*_add_*` helpers с готовым
borrowed byte-string key той же VM. Они сохраняют descriptor kind, owner,
module metadata и finalizer; временные ссылки освобождаются внутри helper.
Имена из общего registry передаются прямо из VM.
Все фиксированные имена production C-кода, включая module-local методы,
исключения, codec aliases и `__future__`, заранее создаются из registry при
инициализации VM. Поля этих строк имеют префикс `internal_`, например
`internal_special_length_key` и `internal_func_code_key`. Source guard запрещает
внутреннему production-коду обращаться к ленивой фабрике C-literal keys.
`TINYPY_INTERNAL_STRING(vm, "literal")` остаётся для extension/test literals:
длина вычисляется из литерала, включая embedded NUL, VM вычисляется один раз.
Макрос возвращает borrowed interned строку, удерживаемую VM до shutdown;
она не требует DECREF. Для владеющей ссылки используется `TINYPY_RET`.
Вне registry эти литералы удерживаются в ленивом VM dictionary. Обычные
runtime strings проверяют общий intern table, но при промахе не добавляются
ни в него, ни в dictionary внутренних C literals. Совпадающий literal не
меняет non-interned compiler preset: для него используется отдельная строка.

`TINYPY_NAME_EQ(name, preset)` сначала сравнивает указатели, затем байты и
длину для разных объектов. Fallback сохраняет Unicode и str subclasses,
не вызывая их пользовательский `__eq__`. Разные указатели сами по себе
не означают разные строки. `func_code` и `__code__` — разные имена одного
атрибута Python 2; оба alias проверяются отдельно готовыми полями VM.
Индексные таблицы операторов и native wrapper slots содержат offsets готовых
полей VM. `tinypy_internal_object_special_operator_key(vm, index)` возвращает
borrowed preset без аллокации и изменения refcount; index должен быть меньше
`TINYPY_SPECIAL_OPERATOR_COUNT`. Отдельный массив строк и дополнительные
владеющие ссылки для операторов не создаются. AUTO wrapper classification
сравнивает полный key с preset, включая raw/Unicode fallback и embedded NUL.
Bound builtin slots имеют отдельный VM type `method-wrapper`, сохраняя общий
native-function payload и public value kind. Обычные bound C methods сохраняют
type `builtin_function_or_method`; metadata используют стандартные member/getset
descriptors, а их free list учитывает фактический type owner. Bytearray iteration
использует `bytearray_iterator`; str/unicode subtype iteration сохраняет generic
`__getitem__` и `__len__` protocols.
Фиксированные codec aliases сравниваются с presets с сохранением нормализации;
проверки имён, у которых Python 2 учитывает только префикс до NUL, сохраняют
это поведение.

Compiler literals, filename и non-interned
marshal payloads обходят lookup intern table; при свёртке констант
interned результат копируется перед изменением serialization policy.

`TINYPY_RET(value)` возвращает тот же ненулевой объект с увеличенным refcount,
вычисляя аргумент ровно один раз. `TINYPY_RET_NONE`, `TINYPY_RET_TRUE`,
`TINYPY_RET_FALSE`, `TINYPY_RET_NOT_IMPLEMENTED`, `TINYPY_RET_ELLIPSIS`,
`TINYPY_RET_EMPTY_TUPLE`, `TINYPY_RET_EMPTY_STRING` и
`TINYPY_RET_EMPTY_UNICODE` принимают VM и возвращают
владеющую ссылку на соответствующий существующий singleton без аллокации.

Python-класс сохраняет исходный объект `str` своего имени, включая subtype.
Native type с квалифицированным C-именем возвращает часть после последней
точки в `__name__` и префикс в `__module__`. Для builtin exceptions с коротким
C-именем используется явно зарегистрированный модуль. Instance descriptor
`function.__module__` не является модулем самого типа `function`.
Модуль Python-класса читается из его собственного namespace.

Логические значения C имеют тип `tinypy_bool_t` и именованные константы
`TINYPY_TRUE`/`TINYPY_FALSE`. Проверка на `!= TINYPY_FALSE` сохраняет семантику
любого ненулевого значения; числовые статусы и битовые маски остаются числами.
Эти константы отличаются от Python singletons, которые возвращают `TINYPY_RET_TRUE`
и `TINYPY_RET_FALSE`.

Getter встроенных атрибутов выбирает функцию из неизменяемых таблиц по типу
объекта и `attribute_id`. Каждый byte-string содержит borrowed указатель на
неизменяемые `tinypy_internal_string_metadata_t`; у VM presets он указывает на
общие для всех VM static metadata с `builtin_attribute_id`, у обычных строк
равен NULL. Нулевой id обозначает отсутствие специализированного getter.
Обычная строка, совпадающая с interned preset, получает сам preset вместе с его
metadata. Копии immutable subtypes и raw строки не наследуют metadata;
изменение содержимого при росте строки сбрасывает указатель. Metadata не
содержат Python values, не удерживают ссылки и не входят в marshal payload.
Чтение id из preset не требует хеширования или сравнения имён.
Для raw/Unicode keys preset заимствуется из общего неизменяемого кеша
внутренних имён VM, после чего id читается из metadata. Отдельная таблица
builtin-имён не создаётся; входящий key не заменяется, не удерживается и
не добавляется в кеш. Lookup сравнивает полное содержимое без пользовательских
`__hash__`/`__eq__`; коллизии и embedded NUL не сокращают сравниваемый span.
Общие getters `__class__`,
`__call__`, `__dict__`, function-code aliases и module dictionary сохраняют
прямой путь. Приоритет custom hooks, дескрипторов и instance dictionary сохраняется.

Python-visible bundled surface намеренно ограничен memory-only runtime:

- встроены `__builtin__`, `sys`, `exceptions`, `__future__`, `_codecs`,
  `_functools`, `_weakref`, `_struct`, `_sre` и `copy_reg`;
- `_codecs` гарантирует ASCII, Latin-1, UTF-8 и transform codec `hex`; остальные
  encodings должен предоставить host search function;
- `_struct` гарантирует формат `d` с repeat counts и byte-order prefixes, а
  также `Struct`, `pack`, `unpack`, `pack_into` и `unpack_from`; `_struct.error`
  является `ValueError`. `Struct` хранит compiled format в native payload,
  предоставляет readonly `format`/`size` getsets и поддерживает weakrefs без
  instance dictionary. Unicode format преобразуется в ASCII, embedded NUL
  завершает parsed format. Module calls используют bounded format cache на
  100 entries; instance methods используют собственный compiled snapshot;
- `_sre` исполняет Python 2.7 regex programs; разбор replacement templates и
  `Match.expand` используют `re._subx` и `re._expand`, предоставленные host;
- filesystem-backed standard library, source-encoding discovery, process
  metadata, environment-dependent `sys` paths и standard I/O input не
  эмулируются. Их предоставляет host либо импортированный memory artifact.

## 7. Bytecode runtime

Interpreter выполняет Python 2.7 opcodes и проверяет code objects до запуска.
Verifier контролирует:

- instruction boundaries;
- argument width и `EXTENDED_ARG`;
- jump targets;
- block-stack transitions;
- value-stack depth;
- finally/with reason markers, их ширину и глубину продолжения;
- indices names/constants/locals/free variables;
- configured instruction и stack limits.

Нормальный вход в cleanup требует доказанного None constant; verifier получает
его признак из constants code object. Изменение или потребление reason marker
сбрасывает доказательство, а cleanup без известного marker отвергается.
Это структурная проверка CFG и стека: типы произвольных Python-операндов и
эффекты пользовательских callbacks проверяются runtime.

`code()` и загрузчик marshal дополнительно требуют `co_nlocals ==
len(co_varnames)` и места для аргументов: frame размечается по первому
значению, а verifier проверяет local operands по второму. CPython принимает
такие code objects, tinypy их отвергает.

Frame execution поддерживает closures, generators, exception blocks,
comprehensions, `with`, imports и tracing data code object.

`f_exc_type`, `f_exc_value` и `f_exc_traceback` показывают сохранённое
состояние вызывающего frame; текущий обработчик читается через `sys.exc_info()`.
Эти getset descriptors допускают запись и удаление. `None` очищает поле;
при возврате сохранённое состояние передаётся VM с сохранением владения.
`f_locals`, `f_restricted` и имя generator являются readonly getsets и
возвращают `AttributeError` при попытке записи или удаления.

При выходе из frame его fast locals освобождаются сразу, даже если frame
остаётся достижим через traceback: без cyclic GC это разрывает циклы
frame -> locals -> frame. Это намеренное отличие от CPython.

## 8. Compiler

Pipeline:

```text
source decoding
-> tokenizer
-> parser and CST
-> AST
-> future scan
-> optional build-constant preprocessing
-> optional metatemplate expansion
-> symbol table
-> basic blocks
-> bytecode generation
-> stack-depth calculation
-> peephole optimization
-> code object
```

Frontend реализован внутри `src/compiler` на объектах, arena allocator и
diagnostics tinypy. Алгоритмы совместимости Python 2.7.18 и их происхождение
зафиксированы в `LICENSES/README.md`; текст PSF license находится в
`LICENSES/PSF-2.0.txt`. В дереве нет копии исходников CPython, его compatibility
headers или зависимости от CPython runtime.

Compiler принимает только sized memory buffers. Logical filename копируется в
code object и diagnostics, но никогда не открывается.

Source decoder поддерживает:

- byte и Unicode source;
- UTF-8 BOM;
- PEP 263 cookie в первых двух строках;
- ASCII, UTF-8 и Latin-1 aliases;
- CRLF и CR normalization;
- embedded NUL diagnostics;
- structured syntax, indentation, tab и decoding errors.

Numeric parsing locale-independent. Integer literal создаёт integer при
попадании в signed 64-bit range, иначе arbitrary-precision long. Float и
complex сохраняют binary value; float literal любой длины округляется
корректно, при этом разбор учитывает не более 800 значащих цифр и sticky
digit, чего достаточно для точного округления double.

Compiler воспроизводит closures, cells, class scopes, name mangling, generators,
comprehensions, future flags, code flags, constants ordering, line table и
nested code objects Python 2.7.

Односимвольные и пустые byte strings являются разделяемыми interned
singletons VM, как `characters[]` в CPython. Escape-decoded односимвольный
литерал и односимвольный результат `%`-форматирования при свёртке констант
создаются отдельными non-interned объектами, поэтому marshal output совпадает
с CPython 2.7.18 побайтово, а разделяемые строки никогда не меняют свои флаги.

Optimization levels:

- `0`: assertions и docstrings сохраняются, `__debug__ == True`;
- `1`: assertions удаляются;
- `2`: assertions и docstrings удаляются.

Присваивание `__debug__` является compile error.

## 9. Compiler API

Основные функции:

```c
tinypy_value_t *tinypy_compile_source(...);
tinypy_value_t *tinypy_eval_source(...);
tinypy_value_t *tinypy_exec_source(...);
tinypy_value_t *tinypy_eval_code(...);
tinypy_value_t *tinypy_exec_code(...);
```

`tinypy_compile_options_t` задаёт mode, future flags, `dont_inherit`, optimize,
feature flags, limits и optional immutable build profile.

При `dont_inherit == 0` явно переданные flags объединяются с future flags
текущего frame. Imports компилируют source с `dont_inherit == 1`.

Compiler limits охватывают:

- source bytes;
- tokens;
- CST и AST nodes;
- nesting: глубина parser stack и глубина AST выражения, включая плоские
  цепочки бинарных операторов, attribute, call и subscript trailers;
- symbols;
- basic blocks;
- instructions;
- constants и constant bytes;
- compiler arena bytes;
- preprocessor operations, value nodes и bytes;
- template expansions, depth и generated AST nodes;
- expanded source bytes и source-map entries.

Временные compiler allocations живут в call-local arena и освобождаются целиком
при success или error.

## 10. Build profiles

Immutable build profile хранит typed constants и deterministic digest.
Поддерживаемые значения:

- `None`, bool, integer, long и float;
- byte string и Unicode;
- immutable tuple из поддерживаемых значений.

Зарезервированный формат build constant:

```text
^__[A-Z][A-Z0-9_]*__$
```

Profile полностью копирует входные данные через host allocator. Input order не
влияет на canonical ordering и digest. Optimize level входит в profile.

При включённом `TINYPY_COMPILE_FEATURE_PREPROCESSOR` каждое чтение bare name
зарезервированного формата заменяется literal AST до symbol table. Атрибуты
вида `object.__STATE__` не являются build constants. Отсутствующая константа и
любая попытка binding зарезервированного имени являются compile error.

`__NDEBUG__` всегда создаётся самим profile: optimize `0` даёт `False`, optimize
`1` и `2` дают `True`. Передать эту константу явно нельзя.

Pure evaluator поддерживает literals и literal containers, boolean, unary,
arithmetic и bitwise operations, comparisons, membership и identity с
`None`/bool. Полностью вычислимый `if` заменяется выбранной suite до symbol
analysis. Calls, attributes, subscripts, lambdas и comprehensions оставляют
условие runtime, хотя build constants внутри него уже заменены.

Build constants заменяются и внутри целей присваивания: augmented assignment,
`for`, `with`, `except` и comprehension targets. Suite, полностью удалённая
preprocessing, заменяется `pass` в позиции своего первого statement, а
опустевший `else` исчезает целиком, так что line table остаётся монотонной.
Промежуточные результаты evaluator ограничены до вычисления: целочисленные
результаты `**`, `<<` и `*` не превышают 65536 бит, а строки и
последовательности — `max_preprocessor_bytes`; превышение является compile
error. Preprocessor без build profile не определяет ни одной константы.

## 11. Metatemplates

При `TINYPY_COMPILE_FEATURE_META` имя `meta` является compiler builtin без
import и без runtime binding. Базовая форма:

```python
@meta.template
def ObjectTemplate(TypeName):
    ClassName = meta.concat('Mixin', TypeName)

    @meta.emit(name=ClassName)
    class Generated(object):
        pass

MixinItem = meta.expand(ObjectTemplate, 'Item')
```

Маркер шаблона имеет только явную форму `@meta.template`; bare `@meta` не
распознаётся как metatemplate.

Expansion поддерживает literal arguments, defaults и keywords, staging
assignments/`if`/`for`, `meta.range`, `meta.concat`, несколько `meta.emit`,
generated declaration names, `meta.name`, `meta.getattr`, `meta.setattr`,
`meta.delattr` и `meta.current_class`. Generated class name используется для
private-name mangling и `super`.

Во время expansion не исполняются Python bytecode, imports, I/O или
пользовательские runtime functions. После pass любой оставшийся доступ или
binding имени `meta` является compile error; `import meta.x` запрещён так же,
как `import meta`. Staged tuple и list значения подставляются в generated code
как displays, а не как constants. Generated name не может быть keyword.
Evaluator meta учитывает `from __future__ import division` и те же границы
результатов, что и build preprocessor.

`tinypy_preprocess_source` выполняет тот же preprocessing pipeline и возвращает
owned result с canonical valid Python 2 source, source-map entries и SHA-256
digest карты. Каждая generated declaration связывает generated position,
template position, expansion position и semantic symbol. Canonical source
компилируется в ту же программу: отрицательные числовые литералы и числовые
операнды attribute/subscript заключаются в скобки, byte strings под
`unicode_literals` получают префикс `b`, а complex literal с бесконечной
частью записывается как `complex(...)`.

## 12. Compile environment

Каждый code object graph, полученный из source, разделяет refcounted immutable
compile environment: feature flags, optimize level и optional deep copy build
profile. Поэтому исходный host profile можно уничтожить сразу после compile.

Python-visible `compile`, `eval` и string `exec` наследуют environment текущего
frame. `dont_inherit` отключает только inheritance future flags и не отключает
build profile, meta/preprocessor flags или optimize level. Вызов C API без
текущего frame использует явно переданные options; imports используют поля
module artifact. Marshal code получает environment из size-aware artifact
descriptor, поскольку marshal v2 его не сериализует.

## 13. Imports и host callbacks

Core не строит filesystem paths. Resolver получает canonical module request и
возвращает один из memory artifacts:

- source buffer;
- marshal-v2 code buffer;
- native module descriptor.

Artifact содержит logical filename, canonical name, package metadata и release
callback. `sys.modules` поддерживает packages, circular imports и rollback при
ошибке resolution, compilation или execution.
Если loader при `reload` заменяет зарегистрированный объект, результатом
становится этот объект; исходный модуль сохраняет свой namespace. Replacement
не обязан быть модулем. Обычная повторная загрузка source artifact исполняется
в прежнем namespace.

Output streams, warnings, diagnostics и formatted tracebacks направляются host
callbacks. Callback input действителен только на время вызова, если явно не
указано иное.

## 14. Errors

`tinypy_error_t` — optional owned structured error. Он хранит копии message,
logical filename, source line, line number и column offset.

Recoverable categories включают:

- Python semantic errors;
- syntax, indentation, tab и decoding errors;
- malformed bytecode, marshal и artifacts;
- configured resource limits;
- ABI и profile mismatch;
- host module resolution failure.

Error одновременно устанавливает корректное Python exception state в VM.

При связывании дополнительных `**kwargs` ошибка `__hash__` или сравнения
ключа немедленно передаётся вызывающему коду; тело функции не выполняется.
CPython 2.7.18 в этой ветке игнорирует ошибку вставки в dict и может вернуть
результат с pending C exception. Такое некорректное состояние tinypy не
воспроизводит. Это исключение из сравнения с oracle проверяется отдельной
runtime-регрессией во всех build profiles.

## 13. Marshal и artifacts

Marshal reader и writer работают с raw Python 2.7 marshal-v2 objects. Direct
code dump поддерживает size query и caller-owned output buffer. Writer
сохраняет string interning/reference order и nested code objects.

Artifact header содержит ABI versions, optimize level, profile digest, source
hash, future flags, payload size и checksum. Loader никогда не принимает
неизвестную версию или incompatible payload молча.

## 14. Проверка

Обязательные gates:

- C11 build с warnings-as-errors;
- C и C++ public-header compilation;
- unit tests object model, bytecode runtime, compiler, marshal и artifacts;
- source modes `exec`, `eval`, `single` и dynamic compilation;
- imports, packages, circular imports и failed-import rollback;
- compiler limit boundary tests;
- allocator accounting после success и error;
- independent-VM parallel compilation;
- reentrant compilation из native callback;
- bounded malformed-input tests и fuzz targets;
- ASan и UBSan;
- symbol audit прямых allocator, I/O, environment, process, locale и thread API;
- namespace audit, запрещающий legacy и foreign exported symbols.

После `tinypy_vm_destroy` allocator accounting должен вернуться к нулю при
соблюдении host ownership contract.
