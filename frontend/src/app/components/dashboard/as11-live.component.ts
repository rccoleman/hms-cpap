import { Component, OnDestroy, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { TranslatePipe } from '@ngx-translate/core';
import { CpapApiService } from '../../services/cpap-api.service';
import { As11Live, As11LiveFigure, AS11_LIVE_STALE_S, as11LiveFigures, as11LiveNextPollMs } from './as11-live';

/**
 * The AirSense 11's live figures (leak, mask pressure, respiratory rate...)
 * while it streams therapy. Draws nothing unless the option is on in
 * Settings and the machine is streaming; it asks once, and keeps asking only
 * when the option is on.
 */
@Component({
  selector: 'app-as11-live',
  standalone: true,
  imports: [CommonModule, TranslatePipe],
  template: `
    <div class="section" *ngIf="figures.length">
      <div class="section-header">
        <div class="section-title"><i class="fa-solid fa-tower-broadcast"></i> {{ 'dashboard.as11Live.title' | translate }}</div>
        <div class="section-subtitle" [class.stale]="stale">
          {{ stale ? ('dashboard.as11Live.stale' | translate: { seconds: data?.age_s }) : ('dashboard.as11Live.subtitle' | translate) }}
        </div>
      </div>
      <div class="metrics-row" [class.stale]="stale">
        <div class="rt-card" *ngFor="let f of figures">
          <div class="rt-icon"><i class="fa-solid" [ngClass]="f.icon"></i></div>
          <div class="rt-content">
            <div class="rt-label">{{ 'dashboard.as11Live.' + f.label | translate }}</div>
            <div class="rt-value">{{ f.value }}<span class="rt-unit" *ngIf="f.unit"> {{ f.unit }}</span></div>
          </div>
        </div>
      </div>
    </div>
  `,
  styles: [`
    .section { margin-bottom: 1.5rem; }
    .section-header { margin-bottom: 0.75rem; }
    .section-title { font-size: 1.05rem; font-weight: 600; color: #e0e0e0; }
    .section-subtitle { font-size: 0.8rem; color: #888; margin-top: 0.2rem; }
    .section-subtitle.stale { color: #f0a050; }
    .metrics-row { display: flex; flex-wrap: wrap; gap: 0.75rem; }
    .metrics-row.stale { opacity: 0.5; }
    .rt-card { display: flex; align-items: center; gap: 0.75rem; flex: 1 1 160px; min-width: 160px;
      background: #1e1e2f; border: 1px solid #333; border-radius: 8px; padding: 0.9rem 1rem; }
    .rt-icon { font-size: 1.1rem; color: #60a5fa; }
    .rt-label { font-size: 0.75rem; color: #888; text-transform: uppercase; letter-spacing: 0.5px; }
    .rt-value { font-size: 1.5rem; font-weight: 700; color: #e0e0e0; }
    .rt-unit { font-size: 0.8rem; font-weight: 400; color: #888; }
  `],
})
export class As11LiveComponent implements OnInit, OnDestroy {
  data: As11Live | null = null;
  figures: (As11LiveFigure & { icon: string })[] = [];
  private timer: ReturnType<typeof setTimeout> | null = null;
  private gone = false;

  constructor(private api: CpapApiService) {}

  get stale(): boolean {
    return (this.data?.age_s ?? 0) > AS11_LIVE_STALE_S;
  }

  ngOnInit(): void {
    this.poll();
  }

  ngOnDestroy(): void {
    this.gone = true;
    if (this.timer) clearTimeout(this.timer);
  }

  private poll(): void {
    this.api.getAs11Live().subscribe({
      next: (d) => this.landed(d),
      error: () => this.landed(null),
    });
  }

  private landed(d: As11Live | null): void {
    if (this.gone) return;
    if (d) {
      this.data = d;
      this.figures = as11LiveFigures(d);
    }
    const next = as11LiveNextPollMs(d);
    if (next !== null) this.timer = setTimeout(() => this.poll(), next);
  }
}
