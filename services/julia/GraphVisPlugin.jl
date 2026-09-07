#!/usr/bin/env julia
# Optional Julia scientific-service example. Bulk arrays use Arrow IPC.
using JSON3
try
    using Arrow
catch
    println(stderr, "Install Arrow.jl and JSON3.jl to use the GraphVis Julia service")
end
for line in eachline(stdin)
    try
        req=JSON3.read(line)
        op=String(get(req,:op,"health"))
        if op=="health"
            println(JSON3.write((ok=true,service="graphvis-julia-science",version="18.4.0",transport="Arrow IPC + JSON control")))
        elseif op=="column.mean"
            tbl=Arrow.Table(String(req[:arrow_path])); col=getproperty(tbl,Symbol(String(req[:column]))); println(JSON3.write((ok=true,result=sum(skipmissing(col))/length(collect(skipmissing(col))))))
        else
            println(JSON3.write((ok=false,error="Unknown operation: $op")))
        end
    catch e
        println(JSON3.write((ok=false,error=string(e))))
    end
    flush(stdout)
end
