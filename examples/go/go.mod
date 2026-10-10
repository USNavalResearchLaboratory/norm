module github.com/USNavalResearchLaboratory/norm/examples/go

go 1.21

// The examples live outside the binding's module tree (src/go) to match the
// examples/<lang> layout of the other language bindings. The replace resolves
// the binding from its in-repo location so the examples build without a tagged
// release and the binding module stays dependency-free.
require github.com/USNavalResearchLaboratory/norm/src/go v0.0.0

replace github.com/USNavalResearchLaboratory/norm/src/go => ../../src/go
