CREATE TABLE TABLE_A (
    COLUMN_1 INTEGER,
    COLUMN_2 INTEGER,
    COLUMN_3 INTEGER,
    COLUMN_4 INTEGER,
    COLUMN_5 INTEGER
);

INSERT INTO TABLE_A (
    COLUMN_1,
    COLUMN_2,
    COLUMN_3,
    COLUMN_4,
    COLUMN_5 )
VALUES (
    1,
    NULL,
    NULL,
    NULL,
    NULL
);

SELECT * FROM TABLE_A;

UPDATE TABLE_A
  SET COLUMN_2 = 1;

SELECT * FROM TABLE_A;

SELECT * 
FROM   TABLE_A
WHERE  COLUMN_2 IS NULL;

SELECT *
FROM TABLE_A
WHERE COLUMN_2 IS NOT NULL;

SELECT *
FROM    TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_3 IS NULL;

UPDATE TABLE_A
  SET COLUMN_3 = 1;

SELECT *
FROM TABLE_A;

SELECT *
FROM TABLE_A
WHERE COLUMN_2 IS NOT NULL;

SELECT *
FROM TABLE_A
WHERE COLUMN_3 IS NOT NULL;

SELECT *
FROM   TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_3 IS NOT NULL;

SELECT *
FROM   TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_3 IS NOT NULL
AND   COLUMN_4 IS NULL;

SELECT *
FROM   TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_4 IS NULL
AND   COLUMN_3 IS NOT NULL;

UPDATE TABLE_A
  SET COLUMN_4 = 1;

SELECT *
FROM TABLE_A;

SELECT *
FROM TABLE_A
WHERE COLUMN_2 IS NOT NULL;

SELECT *
FROM TABLE_A
WHERE COLUMN_3 IS NOT NULL;

SELECT *
FROM   TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_3 IS NOT NULL;

SELECT *
FROM   TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_3 IS NOT NULL
AND   COLUMN_4 IS NULL;

SELECT *
FROM   TABLE_A
WHERE COLUMN_2 IS NOT NULL
AND   COLUMN_4 IS NULL
AND   COLUMN_3 IS NOT NULL;

