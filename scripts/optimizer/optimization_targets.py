class FpsTarget:
    @staticmethod
    def is_better(new_result, old_result):
        return new_result["fps"] > old_result["fps"]

class PowerTarget:
    @staticmethod
    def is_better(new_result, old_result):
        return new_result["power"] < old_result["power"]