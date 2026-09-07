"""Database, web and cloud data connectors for GraphVis.

Credentials are never written to disk by this module. Persistent connector
profiles store only a user-assigned name, connector kind and a redacted URL or
local path unless ``persist_secret`` is explicitly enabled by the caller.
"""
from __future__ import annotations

import io
import json
import os
import re
import sqlite3
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Iterable, Mapping
from urllib.parse import urlsplit, urlunsplit

import pandas as pd

from graphvis.core.logging import get_logger

LOG = get_logger("connectors")


def redact_url(url: str) -> str:
    try:
        parts = urlsplit(url)
        if parts.username is None:
            return url
        host = parts.hostname or ""
        if parts.port:
            host += f":{parts.port}"
        user = parts.username or ""
        netloc = f"{user}:***@{host}"
        return urlunsplit((parts.scheme, netloc, parts.path, parts.query, parts.fragment))
    except Exception:
        return re.sub(r"(?<=://)[^/@:]+:[^/@]+@", "***:***@", str(url))


@dataclass(slots=True)
class ConnectorProfile:
    name: str
    kind: str  # sql | web | file | cloud
    url: str
    query: str = ""
    options: dict[str, Any] = field(default_factory=dict)
    refresh_seconds: float = 0.0
    persist_secret: bool = False

    def serializable(self) -> dict[str, Any]:
        data = asdict(self)
        if not self.persist_secret:
            data["url"] = redact_url(self.url)
        return data


class SQLConnector:
    """Read SQL query results through SQLite or SQLAlchemy.

    Supported URL examples:
    ``sqlite:///project.db``, ``mysql+pymysql://...``,
    ``oracle+oracledb://...``, ``mssql+pyodbc://...?driver=...`` and generic
    ODBC URLs. Third-party DBAPI drivers are installed separately as needed.
    """

    def __init__(self, url: str) -> None:
        self.url = str(url)

    def read(self, query: str, params: Mapping[str, Any] | None = None) -> pd.DataFrame:
        if self.url.startswith("sqlite:///"):
            path = self.url.removeprefix("sqlite:///")
            # sqlite3's context manager only manages transactions, not the
            # connection itself; an unclosed handle keeps the .db locked on
            # Windows. Close explicitly.
            con = sqlite3.connect(path)
            try:
                return pd.read_sql_query(query, con, params=params)
            finally:
                con.close()
        try:
            from sqlalchemy import create_engine, text
        except Exception as exc:
            raise RuntimeError("Non-SQLite database connectors require SQLAlchemy.") from exc
        engine = create_engine(self.url, pool_pre_ping=True)
        try:
            with engine.connect() as conn:
                return pd.read_sql_query(text(query), conn, params=dict(params or {}))
        finally:
            engine.dispose()

    def tables(self) -> list[str]:
        if self.url.startswith("sqlite:///"):
            path = self.url.removeprefix("sqlite:///")
            with sqlite3.connect(path) as con:
                rows = con.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").fetchall()
            return [str(r[0]) for r in rows]
        try:
            from sqlalchemy import create_engine, inspect
        except Exception as exc:
            raise RuntimeError("Table discovery requires SQLAlchemy.") from exc
        engine=create_engine(self.url,pool_pre_ping=True)
        try:
            return list(inspect(engine).get_table_names())
        finally:
            engine.dispose()


class ADOConnector:
    """Windows ADO/COM reader for Access and other ADO providers.

    The connection string is passed directly to ADODB.Connection, e.g. an
    ACE/OLEDB string for an ``.accdb`` file. This backend is available only
    on Windows with ``pywin32`` and a suitable provider installed.
    """

    def __init__(self, connection_string: str) -> None:
        self.connection_string = str(connection_string)

    def read(self, query: str) -> pd.DataFrame:
        try:
            import win32com.client  # type: ignore
        except Exception as exc:
            raise RuntimeError("ADO connectors require Windows, pywin32 and an installed ADO/OLEDB provider.") from exc
        conn = win32com.client.Dispatch("ADODB.Connection")
        rs = None
        try:
            conn.Open(self.connection_string)
            rs = win32com.client.Dispatch("ADODB.Recordset")
            rs.Open(query, conn)
            columns = [str(rs.Fields.Item(i).Name) for i in range(rs.Fields.Count)]
            rows: list[list[Any]] = []
            while not rs.EOF:
                rows.append([rs.Fields.Item(i).Value for i in range(rs.Fields.Count)])
                rs.MoveNext()
            return pd.DataFrame(rows, columns=columns)
        finally:
            try:
                if rs is not None and rs.State:
                    rs.Close()
            except Exception:
                pass
            try:
                if conn.State:
                    conn.Close()
            except Exception:
                pass


class WebCloudConnector:
    """Read tabular data from HTTP(S), network shares or fsspec cloud URLs."""

    @staticmethod
    def _read_bytes(url: str, **storage_options: Any) -> bytes:
        if url.startswith(("http://", "https://")):
            try:
                import requests
            except Exception as exc:
                raise RuntimeError("HTTP connectors require requests.") from exc
            response=requests.get(url,timeout=float(storage_options.pop("timeout",30)),**{k:v for k,v in storage_options.items() if k in {"headers","auth","verify"}})
            response.raise_for_status(); return response.content
        try:
            import fsspec
        except Exception as exc:
            raise RuntimeError("Cloud/network URL connectors require fsspec.") from exc
        with fsspec.open(url,"rb",**storage_options) as fh:
            return fh.read()

    @classmethod
    def read(cls, url: str, fmt: str | None = None, **options: Any) -> pd.DataFrame:
        raw=cls._read_bytes(url,**options.pop("storage_options",{})); fmt=(fmt or Path(urlsplit(url).path).suffix.lstrip(".")).lower()
        bio=io.BytesIO(raw)
        if fmt in {"csv","txt","tsv","dat","asc"}:
            sep=options.pop("sep", "\t" if fmt=="tsv" else None); return pd.read_csv(bio,sep=sep,engine="python" if sep is None else "c",**options)
        if fmt in {"xlsx","xls","xlsm"}: return pd.read_excel(bio,**options)
        if fmt=="json": return pd.read_json(bio,**options)
        if fmt in {"html","htm"}:
            tables=pd.read_html(io.StringIO(raw.decode(options.pop("encoding","utf-8"),errors="replace"))); return tables[0] if tables else pd.DataFrame()
        if fmt in {"parquet","pq"}: return pd.read_parquet(bio,**options)
        if fmt in {"xml"}: return pd.read_xml(io.BytesIO(raw),**options)
        raise ValueError(f"Unsupported web/cloud format: {fmt or 'unknown'}")


class ConnectorStore:
    """Project-local connector metadata store."""

    def __init__(self, path: str | os.PathLike[str]) -> None:
        self.path=Path(path)
        self.path.parent.mkdir(parents=True,exist_ok=True)

    def load(self) -> list[ConnectorProfile]:
        if not self.path.exists(): return []
        try:
            data=json.loads(self.path.read_text(encoding="utf-8")); return [ConnectorProfile(**item) for item in data if isinstance(item,dict)]
        except Exception:
            LOG.exception("Connector profile load failed: %s",self.path); return []

    def save(self, profiles: Iterable[ConnectorProfile]) -> None:
        self.path.write_text(json.dumps([p.serializable() for p in profiles],indent=2,ensure_ascii=False),encoding="utf-8")


class ConnectorManager:
    def __init__(self, store: ConnectorStore | None = None) -> None:
        self.store=store; self.profiles: dict[str,ConnectorProfile]={p.name:p for p in (store.load() if store else [])}

    def upsert(self, profile: ConnectorProfile) -> None:
        self.profiles[profile.name]=profile
        if self.store: self.store.save(self.profiles.values())

    def remove(self, name: str) -> None:
        self.profiles.pop(name,None)
        if self.store: self.store.save(self.profiles.values())

    def refresh(self, name: str) -> pd.DataFrame:
        profile=self.profiles[name]
        if profile.kind.lower()=="sql": return SQLConnector(profile.url).read(profile.query or "SELECT 1")
        if profile.kind.lower()=="ado": return ADOConnector(profile.url).read(profile.query or "SELECT * FROM table_name")
        return WebCloudConnector.read(profile.url,fmt=profile.options.get("format"),**{k:v for k,v in profile.options.items() if k!="format"})
