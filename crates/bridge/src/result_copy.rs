//! Typed CXX transport for bounded TSV copy policy in result-store.
use crate::{convert, ffi};
use choscordb_result_store::{CopyCell, CopyError, CopyRequest, render_copy_tsv};

fn request(source: ffi::CopyRequestDto) -> Result<CopyRequest, CopyError> {
    if !source.valid_unicode {
        return Err(CopyError::InvalidInput);
    }
    let rows = source
        .rows
        .into_iter()
        .map(|row| {
            row.cells
                .into_iter()
                .map(|cell| {
                    if !cell.selected {
                        return Ok(None);
                    }
                    Ok(Some(CopyCell {
                        original: convert::value(cell.original)
                            .map_err(|_| CopyError::InvalidInput)?,
                        resolved: cell
                            .has_resolved
                            .then(|| convert::value(cell.resolved))
                            .transpose()
                            .map_err(|_| CopyError::InvalidInput)?,
                        inserted_omitted: cell.inserted_omitted,
                    }))
                })
                .collect::<Result<Vec<_>, _>>()
        })
        .collect::<Result<Vec<_>, _>>()?;
    let resolutions = source
        .resolutions
        .into_iter()
        .map(|resolution| {
            Ok((
                resolution
                    .has_original
                    .then(|| convert::value(resolution.original))
                    .transpose()
                    .map_err(|_| CopyError::InvalidInput)?,
                convert::value(resolution.resolved).map_err(|_| CopyError::InvalidInput)?,
            ))
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok(CopyRequest {
        rows,
        resolutions,
        byte_budget: usize::try_from(source.byte_budget).map_err(|_| CopyError::Limit)?,
    })
}

pub fn render_copy_tsv_policy(source: ffi::CopyRequestDto) -> ffi::CopyResultDto {
    match request(source).and_then(render_copy_tsv) {
        Ok(text) => ffi::CopyResultDto {
            text,
            ..Default::default()
        },
        Err(error) => {
            let (code, database_type, reason) = match error {
                CopyError::InvalidInput => ("invalid_input", String::new(), String::new()),
                CopyError::InvalidResolution => {
                    ("invalid_resolution", String::new(), String::new())
                }
                CopyError::WrongResolutionType => {
                    ("wrong_resolution_type", String::new(), String::new())
                }
                CopyError::IncompleteResolution => {
                    ("incomplete_resolution", String::new(), String::new())
                }
                CopyError::Unavailable {
                    database_type,
                    reason,
                } => ("unavailable", database_type, reason),
                CopyError::Deferred => ("deferred", String::new(), String::new()),
                CopyError::Limit => ("limit", String::new(), String::new()),
            };
            ffi::CopyResultDto {
                error: code.into(),
                database_type,
                reason,
                ..Default::default()
            }
        }
    }
}
