#!/usr/bin/env Rscript
# Optional R scientific-service example. Requires jsonlite + arrow.
suppressPackageStartupMessages(library(jsonlite))
suppressPackageStartupMessages(library(arrow))
con <- file("stdin", open="r")
while(length(line <- readLines(con, n=1, warn=FALSE)) > 0) {
  result <- tryCatch({
    req <- fromJSON(line); op <- ifelse(is.null(req$op), "health", req$op)
    if(op == "health") list(ok=TRUE, service="graphvis-r-science", version="18.4.0", transport="Arrow IPC + JSON control")
    else if(op == "column.mean") { tab <- read_ipc_file(req$arrow_path); list(ok=TRUE, result=mean(tab[[req$column]], na.rm=TRUE)) }
    else list(ok=FALSE, error=paste("Unknown operation:", op))
  }, error=function(e) list(ok=FALSE,error=conditionMessage(e)))
  cat(toJSON(result,auto_unbox=TRUE),"\n",sep=""); flush.console()
}
