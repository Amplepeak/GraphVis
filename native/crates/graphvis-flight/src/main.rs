use arrow_flight::flight_service_server::FlightServiceServer;
use graphvis_flight::GraphVisFlightService;
use tonic::transport::Server;

#[tokio::main]
async fn main()->Result<(),Box<dyn std::error::Error>>{
    let addr=std::env::var("GRAPHVIS_FLIGHT_ADDR").unwrap_or_else(|_|"127.0.0.1:50051".into()).parse()?;
    println!("GraphVis Arrow Flight worker listening on {addr}");
    Server::builder().add_service(FlightServiceServer::new(GraphVisFlightService::default())).serve(addr).await?;
    Ok(())
}
