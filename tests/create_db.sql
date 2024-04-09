CREATE DATABASE payDB;
USE payDB;

CREATE TABLE payments
(
    PAYID          int(16) NOT NULL AUTO_INCREMENT,
    AMOUNT          varchar(50) NOT NULL,
    STATUS          varchar(10),
    primary key     (PAYID)
);

ALTER TABLE payments AUTO_INCREMENT = 1;
INSERT INTO payments (AMOUNT, STATUS) VALUES ('500000','REQUEST');
INSERT INTO payments (AMOUNT, STATUS) VALUES ('787878','WAITING');
INSERT INTO payments (AMOUNT, STATUS) VALUES ('787878','TIMEOUT');
INSERT INTO payments (AMOUNT, STATUS) VALUES ('123456','FAILED');
INSERT INTO payments (AMOUNT, STATUS) VALUES ('523456','COMPLETED');
