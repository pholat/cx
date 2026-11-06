# Golden tests

Each `*.cx` file here is transpiled by `cx build` and the generated C is then
compiled by the host compiler (`$CC`, default `cc`). This proves the round trip
`.cx -> generic C` stays buildable.

Run them with:

```
make test
```

To inspect the generated C for a single case:

```
./cx build tests/golden/hello.cx
./cx dump  tests/golden/hello.cx
```
