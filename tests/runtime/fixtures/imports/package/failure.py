try:
    import broken_target
except NameError:
    relative_failure_preserved = True
else:
    relative_failure_preserved = False
