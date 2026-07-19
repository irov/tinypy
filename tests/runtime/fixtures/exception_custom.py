class CustomFailure(Exception):
    def __init__(self, value):
        self.value = value


try:
    raise CustomFailure(42)
except CustomFailure as error:
    result = error.value
