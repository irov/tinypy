import helper
from package import child, fallback, failure, implicit
from star_module import *

assert failure.relative_failure_preserved is True

import_result = helper.value + child.value + implicit.value + fallback.value + exported
