//! Arrow-native dataset storage and DataFusion query execution.
//!
//! Apache Arrow is the canonical in-memory representation.  Pandas is not part
//! of the application core; Python plugins exchange Arrow IPC/Flight streams.

use anyhow::{anyhow, Context, Result};
use arrow::{
    array::{Array, ArrayRef, Float64Array},
    datatypes::DataType,
    record_batch::RecordBatch,
};
use arrow_ipc::{reader::FileReader, writer::FileWriter};
use datafusion::prelude::*;
use graphvis_core::DatasetMeta;
use parking_lot::RwLock;
use std::{collections::HashMap, fs::File, path::Path, sync::Arc};
use uuid::Uuid;
use futures::StreamExt;

#[derive(Clone)]
pub struct ArrowDataset {
    pub meta: DatasetMeta,
    pub batches: Arc<Vec<RecordBatch>>,
}

#[derive(Default, Clone)]
pub struct DatasetStore {
    inner: Arc<RwLock<HashMap<Uuid, ArrowDataset>>>,
}

impl DatasetStore {
    pub fn insert(&self, ds: ArrowDataset) { self.inner.write().insert(ds.meta.id, ds); }
    pub fn get(&self, id: Uuid) -> Option<ArrowDataset> { self.inner.read().get(&id).cloned() }
    pub fn all(&self) -> Vec<ArrowDataset> { self.inner.read().values().cloned().collect() }

    pub fn import_arrow_ipc(&self, source: impl AsRef<Path>, cache_dir: impl AsRef<Path>) -> Result<ArrowDataset> {
        let source = source.as_ref();
        let reader = FileReader::try_new(File::open(source)?, None)?;
        let batches: Vec<RecordBatch> = reader.collect::<std::result::Result<_, _>>()?;
        self.register_batches(source, batches, cache_dir)
    }

    pub async fn import_csv(&self, source: impl AsRef<Path>, cache_dir: impl AsRef<Path>) -> Result<ArrowDataset> {
        let source = source.as_ref().to_path_buf();
        let ctx = SessionContext::new();
        let delimiter = if source.extension().and_then(|x| x.to_str()).is_some_and(|x| x.eq_ignore_ascii_case("tsv")) { b'\t' } else { b',' };
        let df = ctx.read_csv(source.to_string_lossy().as_ref(), CsvReadOptions::new().delimiter(delimiter)).await?;
        let batches = df.collect().await?;
        self.register_batches(&source, batches, cache_dir)
    }

    pub async fn import_parquet(&self, source: impl AsRef<Path>, cache_dir: impl AsRef<Path>) -> Result<ArrowDataset> {
        let source = source.as_ref().to_path_buf();
        let ctx = SessionContext::new();
        let df = ctx.read_parquet(source.to_string_lossy().as_ref(), ParquetReadOptions::default()).await?;
        let batches = df.collect().await?;
        self.register_batches(&source, batches, cache_dir)
    }

    fn register_batches(&self, source: &Path, batches: Vec<RecordBatch>, cache_dir: impl AsRef<Path>) -> Result<ArrowDataset> {
        let first = batches.first().ok_or_else(|| anyhow!("dataset contains no record batches"))?;
        let schema = first.schema();
        let rows = batches.iter().map(RecordBatch::num_rows).sum();
        let columns = schema.fields().iter().map(|f| f.name().to_string()).collect::<Vec<_>>();
        let numeric_columns = schema.fields().iter().filter(|f| matches!(
            f.data_type(), DataType::Int8|DataType::Int16|DataType::Int32|DataType::Int64|
            DataType::UInt8|DataType::UInt16|DataType::UInt32|DataType::UInt64|
            DataType::Float32|DataType::Float64
        )).map(|f| f.name().to_string()).collect::<Vec<_>>();
        let id = Uuid::new_v4();
        let cache_dir = cache_dir.as_ref();
        std::fs::create_dir_all(cache_dir)?;
        let cached = cache_dir.join(format!("{id}.arrow"));
        write_ipc(&cached, &batches)?;
        let ds = ArrowDataset {
            meta: DatasetMeta {
                id,
                name: source.file_stem().and_then(|x| x.to_str()).unwrap_or("dataset").to_string(),
                source: source.to_string_lossy().into_owned(),
                arrow_ipc_path: cached.to_string_lossy().into_owned(),
                rows,
                columns,
                numeric_columns,
                units: HashMap::new(),
            },
            batches: Arc::new(batches),
        };
        self.insert(ds.clone());
        Ok(ds)
    }
}

pub fn write_ipc(path: &Path, batches: &[RecordBatch]) -> Result<()> {
    let first = batches.first().context("no record batches")?;
    let mut writer = FileWriter::try_new(File::create(path)?, first.schema().as_ref())?;
    for batch in batches { writer.write(batch)?; }
    writer.finish()?;
    Ok(())
}

pub struct QueryEngine {
    ctx: SessionContext,
}

impl Default for QueryEngine {
    fn default() -> Self { Self { ctx: SessionContext::new() } }
}

impl QueryEngine {
    pub fn register_dataset(&self, name: &str, ds: &ArrowDataset) -> Result<()> {
        let table = datafusion::datasource::MemTable::try_new(ds.batches[0].schema(), vec![ds.batches.as_ref().clone()])?;
        self.ctx.register_table(name, Arc::new(table))?;
        Ok(())
    }

    pub async fn sql(&self, query: &str) -> Result<Vec<RecordBatch>> {
        Ok(self.ctx.sql(query).await?.collect().await?)
    }

    pub async fn sql_to_ipc(&self, query: &str, output: impl AsRef<Path>) -> Result<(usize, usize)> {
        let df = self.ctx.sql(query).await?;
        let mut stream = df.execute_stream().await?;
        let mut writer: Option<FileWriter<File>> = None;
        let mut batches = 0usize;
        let mut rows = 0usize;
        while let Some(batch) = stream.next().await {
            let batch = batch?;
            if writer.is_none() { writer = Some(FileWriter::try_new(File::create(output.as_ref())?, batch.schema().as_ref())?); }
            if let Some(w) = writer.as_mut() { w.write(&batch)?; }
            batches += 1; rows += batch.num_rows();
        }
        let mut writer = writer.context("query returned no record batches")?;
        writer.finish()?;
        Ok((batches, rows))
    }
}


pub fn numeric_column(ds: &ArrowDataset, column: &str) -> Result<Vec<f64>> {
    let mut out = Vec::new();
    for batch in ds.batches.iter() {
        let index = batch.schema().index_of(column)?;
        let arr: ArrayRef = batch.column(index).clone();
        let cast = arrow::compute::cast(&arr, &DataType::Float64)?;
        let values = cast.as_any().downcast_ref::<Float64Array>().context("cast to Float64 failed")?;
        out.extend((0..values.len()).map(|i| if values.is_null(i) { f64::NAN } else { values.value(i) }));
    }
    Ok(out)
}
