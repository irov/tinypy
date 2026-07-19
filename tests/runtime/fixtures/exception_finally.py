marker = 0


def run():
    global marker
    try:
        return 42
    finally:
        marker = 1


result = run()
