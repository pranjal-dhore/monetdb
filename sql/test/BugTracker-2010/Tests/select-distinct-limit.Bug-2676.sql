start transaction;
create table t2676 (id int);
insert into t2676 values (1);
insert into t2676 values (1);
insert into t2676 values (2);
insert into t2676 values (2);
insert into t2676 values (3);
insert into t2676 values (3);
insert into t2676 values (4);
insert into t2676 values (4);
select distinct id from t2676;
select distinct id from t2676 limit 2;
rollback;