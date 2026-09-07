//! Apache Arrow Flight transport for GraphVis remote/HPC workers.
//!
//! Bulk scientific arrays are transported as Arrow RecordBatches. Control
//! metadata may be small JSON bytes in Flight actions, but numerical payloads
//! never use JSON arrays.

use arrow::record_batch::RecordBatch;
use arrow_flight::{
    decode::FlightRecordBatchStream,
    encode::FlightDataEncoderBuilder,
    flight_service_server::FlightService,
    Action, ActionType, Criteria, Empty, FlightData, FlightDescriptor, FlightEndpoint, FlightInfo,
    HandshakeRequest, HandshakeResponse, PollInfo, PutResult, SchemaResult, Ticket,
};
use futures::{stream, Stream, StreamExt, TryStreamExt};
use datafusion::{datasource::MemTable, prelude::SessionContext};
use uuid::Uuid;
use parking_lot::RwLock;
use std::{collections::HashMap, pin::Pin, sync::Arc};
use tonic::{Request, Response, Status, Streaming};

type TonicStream<T> = Pin<Box<dyn Stream<Item=T> + Send + 'static>>;

#[derive(Clone, Default)]
pub struct GraphVisFlightService {
    datasets: Arc<RwLock<HashMap<String, Vec<RecordBatch>>>>,
}

impl GraphVisFlightService {
    pub fn insert(&self, name: impl Into<String>, batches: Vec<RecordBatch>) { self.datasets.write().insert(name.into(), batches); }
    fn key_from_descriptor(d:&FlightDescriptor)->String {
        if !d.path.is_empty() { d.path.join("/") } else { String::from_utf8_lossy(&d.cmd).to_string() }
    }
}

#[tonic::async_trait]
impl FlightService for GraphVisFlightService {
    type HandshakeStream = TonicStream<Result<HandshakeResponse,Status>>;
    type ListFlightsStream = TonicStream<Result<FlightInfo,Status>>;
    type DoGetStream = TonicStream<Result<FlightData,Status>>;
    type DoPutStream = TonicStream<Result<PutResult,Status>>;
    type DoActionStream = TonicStream<Result<arrow_flight::Result,Status>>;
    type ListActionsStream = TonicStream<Result<ActionType,Status>>;
    type DoExchangeStream = TonicStream<Result<FlightData,Status>>;

    async fn handshake(&self, _request:Request<Streaming<HandshakeRequest>>)->Result<Response<Self::HandshakeStream>,Status>{
        Ok(Response::new(Box::pin(stream::iter(vec![Ok(HandshakeResponse{protocol_version:0,payload:"graphvis-18".into()})]))))
    }

    async fn list_flights(&self,_:Request<Criteria>)->Result<Response<Self::ListFlightsStream>,Status>{
        let options=arrow_ipc::writer::IpcWriteOptions::default();
        let mut infos=Vec::new();
        for (key,batches) in self.datasets.read().iter(){
            let Some(first)=batches.first() else {continue};
            let message: arrow_flight::IpcMessage=arrow_flight::SchemaAsIpc::new(first.schema().as_ref(),&options).try_into().map_err(|e:arrow::error::ArrowError|Status::internal(e.to_string()))?;
            let descriptor=FlightDescriptor::new_path(vec![key.clone()]);
            infos.push(Ok(FlightInfo{schema:message.0,flight_descriptor:Some(descriptor),endpoint:vec![FlightEndpoint::new().with_ticket(Ticket::new(key.clone()))],total_records:batches.iter().map(|b|b.num_rows() as i64).sum(),total_bytes:-1,ordered:false,app_metadata:Default::default()}));
        }
        Ok(Response::new(Box::pin(stream::iter(infos))))
    }

    async fn get_flight_info(&self,request:Request<FlightDescriptor>)->Result<Response<FlightInfo>,Status>{
        let key=Self::key_from_descriptor(request.get_ref());
        let guard=self.datasets.read(); let batches=guard.get(&key).ok_or_else(||Status::not_found("dataset not found"))?;
        let schema=batches.first().ok_or_else(||Status::not_found("dataset empty"))?.schema();
        let options=arrow_ipc::writer::IpcWriteOptions::default();
        let message: arrow_flight::IpcMessage = arrow_flight::SchemaAsIpc::new(schema.as_ref(),&options).try_into().map_err(|e: arrow::error::ArrowError|Status::internal(e.to_string()))?;
        let schema_bytes=message.0;
        Ok(Response::new(FlightInfo{schema:schema_bytes,flight_descriptor:Some(request.into_inner()),endpoint:vec![FlightEndpoint::new().with_ticket(Ticket::new(key.clone()))],total_records:batches.iter().map(|b|b.num_rows() as i64).sum(),total_bytes:-1,ordered:false,app_metadata:Default::default()}))
    }

    async fn poll_flight_info(&self,_:Request<FlightDescriptor>)->Result<Response<PollInfo>,Status>{Err(Status::unimplemented("GraphVis jobs use DoExchange for streaming computation"))}

    async fn get_schema(&self,request:Request<FlightDescriptor>)->Result<Response<SchemaResult>,Status>{
        let key=Self::key_from_descriptor(request.get_ref()); let guard=self.datasets.read(); let batch=guard.get(&key).and_then(|v|v.first()).ok_or_else(||Status::not_found("dataset not found"))?;
        let options=arrow_ipc::writer::IpcWriteOptions::default();
        let message: arrow_flight::IpcMessage = arrow_flight::SchemaAsIpc::new(batch.schema().as_ref(),&options).try_into().map_err(|e: arrow::error::ArrowError|Status::internal(e.to_string()))?;
        let bytes=message.0;
        Ok(Response::new(SchemaResult{schema:bytes}))
    }

    async fn do_get(&self,request:Request<Ticket>)->Result<Response<Self::DoGetStream>,Status>{
        let key=String::from_utf8_lossy(&request.into_inner().ticket).to_string();
        let batches=self.datasets.read().get(&key).cloned().ok_or_else(||Status::not_found("ticket not found"))?;
        let input=stream::iter(batches.into_iter().map(Ok::<_,arrow_flight::error::FlightError>));
        let encoded=FlightDataEncoderBuilder::new().build(input).map(|r|r.map_err(|e|Status::internal(e.to_string())));
        Ok(Response::new(Box::pin(encoded)))
    }

    async fn do_put(&self,request:Request<Streaming<FlightData>>)->Result<Response<Self::DoPutStream>,Status>{
        let mut incoming=request.into_inner();
        let first=incoming.next().await.ok_or_else(||Status::invalid_argument("empty DoPut"))??;
        let key=first.flight_descriptor.as_ref().map(Self::key_from_descriptor).unwrap_or_else(||"upload".into());
        // Preserve streaming: decode the first message plus the remaining tonic stream
        // directly instead of buffering all FlightData before Arrow decoding.
        let source=stream::once(async move{Ok::<_,arrow_flight::error::FlightError>(first)})
            .chain(incoming.map_err(|e|e.into()));
        let mut decoded=FlightRecordBatchStream::new_from_flight_data(source);
        let mut batches=Vec::new();
        while let Some(batch)=decoded.next().await{batches.push(batch.map_err(|e|Status::invalid_argument(e.to_string()))?);}
        self.datasets.write().insert(key,batches);
        Ok(Response::new(Box::pin(stream::iter(vec![Ok(PutResult::default())]))))
    }

    async fn do_action(&self,request:Request<Action>)->Result<Response<Self::DoActionStream>,Status>{
        let action=request.into_inner();
        if action.r#type=="graphvis.sql" {
            let query=String::from_utf8(action.body.to_vec()).map_err(|_|Status::invalid_argument("SQL action body must be UTF-8"))?;
            let ctx=SessionContext::new();
            for (name,batches) in self.datasets.read().iter() {
                if let Some(first)=batches.first(){
                    let table=MemTable::try_new(first.schema(),vec![batches.clone()]).map_err(|e|Status::internal(e.to_string()))?;
                    let safe=name.chars().map(|c|if c.is_ascii_alphanumeric(){c}else{'_'}).collect::<String>();
                    ctx.register_table(&safe,Arc::new(table)).map_err(|e|Status::internal(e.to_string()))?;
                }
            }
            let batches=ctx.sql(&query).await.map_err(|e|Status::invalid_argument(e.to_string()))?.collect().await.map_err(|e|Status::internal(e.to_string()))?;
            let ticket=format!("result:{}",Uuid::new_v4()); self.datasets.write().insert(ticket.clone(),batches);
            return Ok(Response::new(Box::pin(stream::iter(vec![Ok(arrow_flight::Result{body:ticket.into()})]))));
        }
        if action.r#type=="graphvis.capabilities" {
            let body=serde_json::json!({"transport":"Arrow Flight","compute":["sql","do_exchange"],"version":"18.4.0"}).to_string();
            return Ok(Response::new(Box::pin(stream::iter(vec![Ok(arrow_flight::Result{body:body.into()})]))));
        }
        Err(Status::unimplemented("unknown GraphVis Flight action"))
    }
    async fn list_actions(&self,_:Request<Empty>)->Result<Response<Self::ListActionsStream>,Status>{Ok(Response::new(Box::pin(stream::iter(vec![Ok(ActionType{r#type:"graphvis.capabilities".into(),description:"Return GraphVis worker capabilities".into()}),Ok(ActionType{r#type:"graphvis.sql".into(),description:"Execute SQL and return a Flight ticket for the Arrow result".into()})]))))}

    async fn do_exchange(&self,request:Request<Streaming<FlightData>>)->Result<Response<Self::DoExchangeStream>,Status>{
        // DoExchange is the high-throughput compute lane. The initial app_metadata
        // identifies an operation; subsequent messages are Arrow batches.  This
        // baseline echoes batches losslessly so client/server plumbing is usable
        // before registering site-specific HPC kernels.
        let incoming=request.into_inner().map(|r|r.map_err(|e|Status::internal(e.to_string())));
        Ok(Response::new(Box::pin(incoming)))
    }
}
