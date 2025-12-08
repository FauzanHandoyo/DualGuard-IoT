# **DualGuard-IoT: Smart Locker with RFID & Distributed ESP32 System**

DualGuard-IoT adalah sistem Smart Locker berbasis IoT yang menggabungkan RFID, ESP32 multiprocessor, MQTT, dan Blynk untuk menghadirkan sistem pengamanan modern yang aman, responsif, serta dapat dipantau secara real time. Proyek ini dikembangkan sebagai Final Project Praktikum Real Time System & IoT – Universitas Indonesia.

---

## **Overview**

DualGuard menggunakan dua modul ESP32 yang bekerja sebagai sistem terdistribusi:

* **Node 1 — Authentication Unit**
  Berfungsi membaca RFID (RC522), memverifikasi UID, dan mengirimkan status autentikasi melalui MQTT.

* **Node 2 — Control Unit**
  Menerima perintah dari MQTT untuk membuka atau mengunci solenoid lock, serta memberikan feedback melalui LED dan buzzer.

Sistem ini terintegrasi dengan:

* **Blynk IoT Dashboard** untuk monitoring status locker secara real time
* **FreeRTOS** untuk manajemen task, queue, dan timer

---

## **Features**

### RFID Authentication

Pembacaan UID RFID akurat untuk memverifikasi pengguna terdaftar.

### Distributed ESP32 Architecture

Dua ESP32 terhubung melalui MQTT untuk meningkatkan keamanan dan fleksibilitas sistem.

### Smart Lock Control

Solenoid lock dioperasikan melalui relay dan dikontrol menggunakan mekanisme auto-lock berbasis software timer.

### Real-Time Monitoring with Blynk

Dashboard menampilkan status loker, hasil autentikasi, dan aktivitas perangkat secara langsung.

### FreeRTOS Integration

Menggunakan task management, queue, sinkronisasi, dan software timer untuk memastikan sistem berjalan paralel, stabil, dan responsif.

---

## **Hardware Components**

| Komponen           | Jumlah |
| ------------------ | ------ |
| ESP32              | 2      |
| RFID RC522         | 1      |
| Solenoid Door Lock | 1      |
| Relay Module       | 1      |
| Buzzer             | 1      |
| LED Indicator      | 1      |
| Breadboard         | 2      |
| Jumper Wires       | 20     |
| RFID Tag / Card    | 1      |

---

## **System Architecture**

```
RFID Tag → ESP32 Authentication Node → MQTT Broker → ESP32 Control Node → Solenoid Lock
```

Komunikasi antar ESP32 dilakukan melalui MQTT, sementara seluruh status perangkat dipantau melalui Blynk.

---

## **Software Structure**

### Authentication Node

* Membaca UID RFID melalui RC522
* Memverifikasi UID pengguna
* Mengirimkan status autentikasi melalui MQTT
* Memperbarui status pada dashboard Blynk

### Control Node

* Menerima pesan dari MQTT
* Menentukan akses valid atau tidak
* Mengaktifkan relay untuk membuka solenoid lock
* Menjalankan timer otomatis untuk penguncian ulang
* Memberikan feedback melalui LED dan buzzer
* Mengirimkan status ke dashboard Blynk

---

## **Documentation**

(Tambahkan foto rangkaian atau hasil proyek pada bagian ini)

---

## **Testing Summary**

Pengujian mencakup:

* Pembacaan RFID secara konsisten dan akurat
* Pengiriman dan penerimaan MQTT tanpa keterlambatan signifikan
* Kontrol solenoid lock bekerja sesuai autentikasi
* Indikator LED dan buzzer memberikan feedback sesuai status
* Dashboard Blynk menampilkan status perangkat secara real time

Hasil pengujian menunjukkan bahwa seluruh fungsi berjalan stabil, responsif, dan memenuhi kriteria penerimaan proyek.

---

## **Conclusion**

DualGuard-IoT berhasil mengimplementasikan sistem smart locker berbasis ESP32, RFID, dan MQTT dengan dukungan Blynk untuk pemantauan jarak jauh. Sistem mampu melakukan autentikasi RFID, mengontrol solenoid lock secara real time, dan menampilkan status perangkat melalui dashboard IoT. Dengan memanfaatkan FreeRTOS, seluruh task berjalan paralel, bebas konflik, dan memberikan performa yang stabil sesuai kebutuhan proyek IoT.

---

## **Team Members (Group 19)**

| Nama                             | NPM        | Role                           |
| -------------------------------- | ---------- | ------------------------------ |
| Reyhan Ahnaf Deannova            | 2306267100 | Hardware, Authentication, Lock |
| Adhi Rajasa Rafif                | 2306266943 | Laporan, PPT                   |
| Izzan Nawa Syarif                | 2306266956 | Hardware, Authentication, Lock |
| Fauzan Farras Hakim Budi Handoyo | 2306250610 | Laporan, PPT, Flowchart        |

---

## **References**

* Modul IoT Digilab UI (Task, Memory, Deadlock, MQTT, Software Timer, Blynk)
* [https://learn.digilabdte.com/books/internet-of-things](https://learn.digilabdte.com/books/internet-of-things)

---

Jika kamu ingin README dibuat lebih singkat, lebih formal, atau ditambahkan diagram arsitektur, tinggal beri tahu.
