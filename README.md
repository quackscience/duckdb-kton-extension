# DuckDB KTON Extension

This extension reads old [DanskeBank Bank Account Statement records](TKU_AccountStatement_KTON_UK.pdf).

This extension is based on the glorious [Quack Zig C API extension](https://github.com/mlafeldt/quack-zig) by [Mathias Lafeldt](https://github.com/mlafeldt).

> Proof that you can write text file table scanner function with the help of AI in less than an evening.

**DISCLAIMER: This extension is not product proofed and probably crashes with invalid formatted data.**

## Building the Extension

Install [Zig](https://ziglang.org) and [uv](https://docs.astral.sh/uv/). That's it. No other dependencies are required.

> See more about the build commands in the original DuckDB Zig repo: [Quack Zig C API extension](https://github.com/mlafeldt/quack-zig).

```shell
zig build
```

## Testing

Run the [SQL logic tests](https://duckdb.org/docs/dev/sqllogictest/intro.html) with `zig build test`.

```shell
zig build test
```

## Using the Extension

```
❯ duckdb -unsigned
v1.1.3 19864453f7
Enter ".help" for usage hints.
D LOAD 'zig-out/v1.1.3/osx_arm64/kton.duckdb_extension';
D SELECT * FROM read_kton('test/data/test.kton');
┌───────────────┬───────────────┬───────────────┬────────────────────┬────────────────────┬──────────────┬────────────┬──────────────┬───┬──────────────┬───────────────┬───────────────────┬──────────────────────┬──────────────────────┬──────────────────────┬───────────┬─────────────┬──────────┐
│ material_code │ record_number │ record_length │ transaction_number │     filing_id      │ booking_date │ value_date │ payment_date │ … │ receipt_code │ transfer_type │ payee_payer_name  │ payee_payer_name_s…  │ payee_account_number │ payee_account_chan…  │ reference │ form_number │ level_id │
│    varchar    │    varchar    │    varchar    │       int32        │      varchar       │     date     │    date    │     date     │   │   varchar    │    varchar    │      varchar      │       varchar        │       varchar        │       varchar        │   int64   │   varchar   │ varchar  │
├───────────────┼───────────────┼───────────────┼────────────────────┼────────────────────┼──────────────┼────────────┼──────────────┼───┼──────────────┼───────────────┼───────────────────┼──────────────────────┼──────────────────────┼──────────────────────┼───────────┼─────────────┼──────────┤
│ T             │ 10            │ 188           │                  2 │ 2312098E5243985673 │ 2023-12-11   │ 2023-12-09 │ 2023-12-09   │ … │              │ A             │ BCDEFGHIJK MATTIA │                      │                      │                      │     15082 │             │          │
├───────────────┴───────────────┴───────────────┴────────────────────┴────────────────────┴──────────────┴────────────┴──────────────┴───┴──────────────┴───────────────┴───────────────────┴──────────────────────┴──────────────────────┴──────────────────────┴───────────┴─────────────┴──────────┤
│ 1 rows                                                                                                                                                                                                                                                                        21 columns (17 shown) │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## License

Licensed under the [MIT License](LICENSE).
