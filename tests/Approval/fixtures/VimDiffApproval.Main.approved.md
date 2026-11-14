# per-line value edits

**initial**
```
int main() {
  int n = 0;
  for(int i = 0; i < n; i++) {
    cout << i << endl;
  }
}
```

**final**
```
int main() {
  int m = 1;
  for(int i = 0; i < m; i++) {
    cout << i+1 << endl;
  }
}
```

_Edit costs: delete / insert / move_

### Plan: cost 31.9

```
n = 0;
->
m = 1;
```
`2 / 7 / 0`

```
n
->
m
```
`1 / 2 / 8.2`

```
->
+1
```
`0 / 4 / 4.7`
