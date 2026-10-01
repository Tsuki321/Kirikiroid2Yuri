import pathlib
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8"
sys.stdout.write(data.decode(encoding))
