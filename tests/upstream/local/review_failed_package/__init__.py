"""A package that fails after importing its subpackage, which stays in
sys.modules."""
from local.review_failed_package.sub import relative
relative.RUNS.append(__name__)
raise RuntimeError('package fails')
